/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tas.h>
#include <tas_memif.h>
#include <packet_defs.h>
#include <utils.h>
#include <utils_timeout.h>
#include <utils_sync.h>
#include "internal.h"

#include <rte_config.h>
#include <rte_hash_crc.h>

#define PKTBUF_SIZE 1536

struct nic_buffer {
  uint64_t addr;
  void *buf;
};

struct flow_id_item {
  uint32_t flow_id;
  struct flow_id_item *next;
};

static int adminq_init(void);
static int adminq_init_core(uint16_t core);
static inline int rxq_poll(void);
static inline void process_packet(const void *buf, uint16_t len,
    uint32_t fn_core, uint16_t flow_group);
static inline volatile struct flextcp_pl_ktx *ktx_try_alloc(uint32_t core,
    struct nic_buffer **buf, uint32_t *new_tail);
static inline uint32_t flow_hash(ip_addr_t lip, beui16_t lp,
    ip_addr_t rip, beui16_t rp);
static inline int flow_slot_alloc(uint32_t h, uint32_t *i, uint32_t *d);
static inline int flow_slot_clear(uint32_t f_id, ip_addr_t lip, beui16_t lp,
    ip_addr_t rip, beui16_t rp);
static void flow_id_alloc_init(void);
static int flow_id_alloc(uint32_t *fid);
static void flow_id_free(uint32_t flow_id);

struct flow_id_item flow_id_items[FLEXNIC_PL_FLOWST_NUM];
struct flow_id_item *flow_id_freelist;

static uint32_t fn_cores;

static struct nic_buffer **rxq_bufs;
static volatile struct flextcp_pl_krx **rxq_base;
static uint32_t rxq_len;
static uint32_t *rxq_tail;
static uint32_t rxq_next;

static struct nic_buffer **txq_bufs;
static volatile struct flextcp_pl_ktx **txq_base;
static uint32_t txq_len;
static uint32_t *txq_tail;

int nicif_init(void)
{
  rte_hash_crc_init_alg();

  /* wait for fastpath to be ready */
  while (!(tas_info->flags & FLEXNIC_FLAG_READY));

  fn_cores = tas_info->cores_num;

  /* prepare packet memory manager */
  if (packetmem_init()) {
    fprintf(stderr, "nicif_init: pktmem_init failed\n");
    return -1;
  }

  /* prepare flow_id allocator */
  flow_id_alloc_init();

  if (adminq_init()) {
    fprintf(stderr, "nicif_init: initializing admin queue failed\n");
    return -1;
  }

  return 0;
}

unsigned nicif_poll(void)
{
  unsigned i, ret = 0/*, nonsuc = 0*/;
  int x;

  for (i = 0; i < 512; i++) {
    x = rxq_poll();
    /*if (x == -1 && ++nonsuc > 2 * fn_cores)
      break;
    else if (x != -1)
      nonsuc = 0;*/

    ret += (x == -1 ? 0 : 1);
  }

  return ret;
}

/** Register application context */
int nicif_appctx_add(uint16_t appid, uint32_t db, uint64_t *rxq_base,
    uint32_t rxq_len, uint64_t *txq_base, uint32_t txq_len, int evfd)
{
  struct flextcp_pl_appctx *actx;
  struct flextcp_pl_appst *ast = &fp_state->appst[appid];
  uint16_t i;

  if (appid >= FLEXNIC_PL_APPST_NUM) {
    fprintf(stderr, "nicif_appctx_add: app id too high (%u, max=%u)\n", appid,
        FLEXNIC_PL_APPST_NUM);
    return -1;
  }

  if (ast->ctx_num + 1 >= FLEXNIC_PL_APPST_CTX_NUM) {
    fprintf(stderr, "nicif_appctx_add: too many contexts in app\n");
    return -1;

  }

  for (i = 0; i < tas_info->cores_num; i++) {
    actx = &fp_state->appctx[i][db];
    actx->appst_id = appid;
    actx->rx_base = rxq_base[i];
    actx->tx_base = txq_base[i];
    actx->rx_avail = rxq_len;
    actx->evfd = evfd;
  }

  MEM_BARRIER();

  for (i = 0; i < tas_info->cores_num; i++) {
    actx = &fp_state->appctx[i][db];
    actx->tx_len = txq_len;
    actx->rx_len = rxq_len;
  }

  MEM_BARRIER();
  ast->ctx_ids[ast->ctx_num] = db;
  MEM_BARRIER();
  ast->ctx_num++;

  return 0;
}

/** Register flow */
int nicif_connection_add(uint32_t db, uint64_t mac_remote, uint32_t ip_local,
    uint16_t port_local, uint32_t ip_remote, uint16_t port_remote,
    uint64_t rx_base, uint32_t rx_len, uint64_t tx_base, uint32_t tx_len,
    uint32_t remote_seq, uint32_t local_seq, uint64_t app_opaque,
    uint32_t flags, uint32_t rate, uint32_t fn_core, uint16_t flow_group,
    uint32_t *pf_id)
{
  struct flextcp_pl_flowst *fs;
  beui32_t lip = t_beui32(ip_local), rip = t_beui32(ip_remote);
  beui16_t lp = t_beui16(port_local), rp = t_beui16(port_remote);
  uint32_t i, d, f_id, hash;
  struct flextcp_pl_flowhte *hte = fp_state->flowht;

  /* allocate flow id */
  if (flow_id_alloc(&f_id) != 0) {
    fprintf(stderr, "nicif_connection_add: allocating flow state\n");
    return -1;
  }

  /* calculate hash and find empty slot */
  hash = flow_hash(lip, lp, rip, rp);
  if (flow_slot_alloc(hash, &i, &d) != 0) {
    flow_id_free(f_id);
    fprintf(stderr, "nicif_connection_add: allocating slot failed\n");
    return -1;
  }
  assert(i < FLEXNIC_PL_FLOWHT_ENTRIES);
  assert(d < FLEXNIC_PL_FLOWHT_NBSZ);

  if ((flags & NICIF_CONN_ECN) == NICIF_CONN_ECN) {
    rx_base |= FLEXNIC_PL_FLOWST_ECN;
  }

  fs = &fp_state->flowst[f_id];
  fs->opaque = app_opaque;
  fs->rx_base_sp = rx_base;
  fs->tx_base = tx_base;
  fs->rx_len = rx_len;
  fs->tx_len = tx_len;
  memcpy(&fs->remote_mac, &mac_remote, ETH_ADDR_LEN);
  fs->db_id = db;

  fs->local_ip = lip;
  fs->remote_ip = rip;
  fs->local_port = lp;
  fs->remote_port = rp;

  fs->flow_group = flow_group;
  fs->lock = 0;
  fs->bump_seq = 0;

  fs->rx_avail = rx_len;
  fs->rx_next_pos = 0;
  fs->rx_next_seq = remote_seq;
  fs->rx_remote_avail = rx_len; /* XXX */

  fs->tx_sent = 0;
  fs->tx_next_pos = 0;
  fs->tx_next_seq = local_seq;
  fs->tx_avail = 0;
  fs->tx_next_ts = 0;
  fs->tx_rate = rate;
  fs->rtt_est = 0;

  /* write to empty entry first */
  MEM_BARRIER();
  hte[i].flow_hash = hash;
  MEM_BARRIER();
  hte[i].flow_id = FLEXNIC_PL_FLOWHTE_VALID |
      (d << FLEXNIC_PL_FLOWHTE_POSSHIFT) | f_id;

  *pf_id = f_id;
  return 0;
}

int nicif_connection_disable(uint32_t f_id, uint32_t *tx_seq, uint32_t *rx_seq,
    int *tx_closed, int *rx_closed)
{
  struct flextcp_pl_flowst *fs = &fp_state->flowst[f_id];

  util_spin_lock(&fs->lock);

  *tx_seq = fs->tx_next_seq;
  *rx_seq = fs->rx_next_seq;
  fs->rx_base_sp |= FLEXNIC_PL_FLOWST_SLOWPATH;

  *rx_closed = !!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_RXFIN);
  *tx_closed = !!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_TXFIN) &&
      fs->tx_sent == 0;

  util_spin_unlock(&fs->lock);

  flow_slot_clear(f_id, fs->local_ip, fs->local_port, fs->remote_ip,
      fs->remote_port);
  return 0;
}

void nicif_connection_free(uint32_t f_id)
{
  flow_id_free(f_id);
}

/** Move flow to new db */
int nicif_connection_move(uint32_t dst_db, uint32_t f_id)
{
  fp_state->flowst[f_id].db_id = dst_db;
  return 0;
}

/** Read connection stats from NIC. */
int nicif_connection_stats(uint32_t f_id,
    struct nicif_connection_stats *p_stats)
{
  struct flextcp_pl_flowst *fs;

  if (f_id >= FLEXNIC_PL_FLOWST_NUM) {
    fprintf(stderr, "nicif_connection_stats: bad flow id\n");
    return -1;
  }

  fs = &fp_state->flowst[f_id];
  p_stats->c_drops = fs->cnt_tx_drops;
  p_stats->c_acks = fs->cnt_rx_acks;
  p_stats->c_ackb = fs->cnt_rx_ack_bytes;
  p_stats->c_ecnb = fs->cnt_rx_ecn_bytes;
  p_stats->txp = fs->tx_sent != 0;
  p_stats->rtt = fs->rtt_est;

  return 0;
}

/**
 * Set rate for flow.
 *
 * @param f_id  ID of flow
 * @param rate  Rate to set [Kbps]
 *
 * @return 0 on success, <0 else
 */
int nicif_connection_setrate(uint32_t f_id, uint32_t rate)
{
  struct flextcp_pl_flowst *fs;

  if (f_id >= FLEXNIC_PL_FLOWST_NUM) {
    fprintf(stderr, "nicif_connection_stats: bad flow id\n");
    return -1;
  }

  fs = &fp_state->flowst[f_id];
  fs->tx_rate = rate;

  return 0;
}

/** Mark flow for retransmit after timeout. */
int nicif_connection_retransmit(uint32_t f_id, uint16_t flow_group)
{
  volatile struct flextcp_pl_ktx *ktx;
  struct nic_buffer *buf;
  uint32_t tail;
  uint16_t core = fp_state->flow_group_steering[flow_group];

  if ((ktx = ktx_try_alloc(core, &buf, &tail)) == NULL) {
    return -1;
  }
  txq_tail[core] = tail;

  ktx->msg.connretran.flow_id = f_id;
  MEM_BARRIER();
  ktx->type = FLEXTCP_PL_KTX_CONNRETRAN;

  notify_fastpath_core(core);

  return 0;
}

/** Allocate transmit buffer */
int nicif_tx_alloc(uint16_t len, void **pbuf, uint32_t *opaque)
{
  volatile struct flextcp_pl_ktx *ktx;
  struct nic_buffer *buf;

  if ((ktx = ktx_try_alloc(0, &buf, opaque)) == NULL) {
    return -1;
  }

  ktx->msg.packet.addr = buf->addr;
  ktx->msg.packet.len = len;
  *pbuf = buf->buf;
  return 0;
}

/** Actually send out transmit buffer (lens need to match) */
void nicif_tx_send(uint32_t opaque, int no_ts)
{
  uint32_t tail = (opaque == 0 ? txq_len - 1 : opaque - 1);
  volatile struct flextcp_pl_ktx *ktx = &txq_base[0][tail];

  MEM_BARRIER();
  ktx->type = (!no_ts ? FLEXTCP_PL_KTX_PACKET : FLEXTCP_PL_KTX_PACKET_NOTS);
  txq_tail[0] = opaque;
  
  notify_fastpath_core(0);
}

static int adminq_init(void)
{
  uint32_t i;

  rxq_len = config.nic_rx_len;
  txq_len = config.nic_tx_len;

  rxq_bufs = calloc(fn_cores, sizeof(*rxq_bufs));
  rxq_base = calloc(fn_cores, sizeof(*rxq_base));
  rxq_tail = calloc(fn_cores, sizeof(*rxq_tail));
  txq_bufs = calloc(fn_cores, sizeof(*txq_bufs));
  txq_base = calloc(fn_cores, sizeof(*txq_base));
  txq_tail = calloc(fn_cores, sizeof(*txq_tail));
  if (rxq_bufs == NULL || rxq_base == NULL || rxq_tail == NULL ||
      txq_bufs == NULL || txq_base == NULL || txq_tail == NULL)
  {
    fprintf(stderr, "adminq_init: queue state alloc failed\n");
    return -1;
  }

  rxq_next = 0;

  for (i = 0; i < fn_cores; i++) {
    if (adminq_init_core(i) != 0)
      return -1;
  }

  return 0;
}

static int adminq_init_core(uint16_t core)
{
  struct packetmem_handle *pm_bufs, *pm_rx, *pm_tx;
  uintptr_t off_bufs, off_rx, off_tx;
  size_t i, sz_bufs, sz_rx, sz_tx;

  if ((rxq_bufs[core] = calloc(config.nic_rx_len, sizeof(**rxq_bufs)))
      == NULL)
  {
    fprintf(stderr, "adminq_init: calloc rx bufs failed\n");
    return -1;
  }
  if ((txq_bufs[core] = calloc(config.nic_tx_len, sizeof(**txq_bufs)))
      == NULL)
  {
    fprintf(stderr, "adminq_init: calloc tx bufs failed\n");
    free(rxq_bufs[core]);
    return -1;
  }

  sz_bufs = ((config.nic_rx_len + config.nic_tx_len) * PKTBUF_SIZE + 0xfff)
    & ~0xfffULL;
  if (packetmem_alloc(sz_bufs, &off_bufs, &pm_bufs) != 0) {
    fprintf(stderr, "adminq_init: packetmem_alloc bufs failed\n");
    free(txq_bufs[core]);
    free(rxq_bufs[core]);
    return -1;
  }

  sz_rx = config.nic_rx_len * sizeof(struct flextcp_pl_krx);
  if (packetmem_alloc(sz_rx, &off_rx, &pm_rx) != 0) {
    fprintf(stderr, "adminq_init: packetmem_alloc tx failed\n");
    packetmem_free(pm_bufs);
    free(txq_bufs[core]);
    free(rxq_bufs[core]);
    return -1;
  }
  sz_tx = config.nic_tx_len * sizeof(struct flextcp_pl_ktx);
  if (packetmem_alloc(sz_tx, &off_tx, &pm_tx) != 0) {
    fprintf(stderr, "adminq_init: packetmem_alloc tx failed\n");
    packetmem_free(pm_rx);
    packetmem_free(pm_bufs);
    free(txq_bufs[core]);
    free(rxq_bufs[core]);
    return -1;
  }

  rxq_base[core] = (volatile struct flextcp_pl_krx *)
      ((uint8_t *) tas_shm + off_rx);
  txq_base[core] = (volatile struct flextcp_pl_ktx *)
      ((uint8_t *) tas_shm + off_tx);

  memset((void *) rxq_base[core], 0, sz_rx);
  memset((void *) txq_base[core], 0, sz_tx);

  for (i = 0; i < rxq_len; i++) {
    rxq_bufs[core][i].addr = off_bufs;
    rxq_bufs[core][i].buf = (uint8_t *) tas_shm + off_bufs;
    rxq_base[core][i].addr = off_bufs;
    off_bufs += PKTBUF_SIZE;
  }
  for (i = 0; i < txq_len; i++) {
    txq_bufs[core][i].addr = off_bufs;
    txq_bufs[core][i].buf = (uint8_t *) tas_shm + off_bufs;
    off_bufs += PKTBUF_SIZE;
  }

  fp_state->kctx[core].rx_base = off_rx;
  fp_state->kctx[core].tx_base = off_tx;
  MEM_BARRIER();
  fp_state->kctx[core].tx_len = sz_tx;
  fp_state->kctx[core].rx_len = sz_rx;
  return 0;
}

static inline int rxq_poll(void)
{
  uint32_t old_tail, tail, core;
  volatile struct flextcp_pl_krx *krx;
  struct nic_buffer *buf;
  uint8_t type;
  int ret = 0;

  core = rxq_next;
  old_tail = tail = rxq_tail[core];
  krx = &rxq_base[core][tail];
  buf = &rxq_bufs[core][tail];
  rxq_next = (core + 1) % fn_cores;

  /* no queue entry here */
  type = krx->type;
  if (type == FLEXTCP_PL_KRX_INVALID) {
    return -1;
  }

  /* update tail */
  tail = tail + 1;
  if (tail == rxq_len) {
    tail -= rxq_len;
  }

  /* handle based on queue entry type */
  type = krx->type;
  switch (type) {
    case FLEXTCP_PL_KRX_PACKET:
      process_packet(buf->buf, krx->msg.packet.len, krx->msg.packet.fn_core,
          krx->msg.packet.flow_group);
      break;

    default:
      fprintf(stderr, "rxq_poll: unknown rx type 0x%x old %x len %x\n", type,
          old_tail, rxq_len);
  }

  krx->type = 0;
  rxq_tail[core] = tail;

  return ret;
}

static inline void process_packet(const void *buf, uint16_t len,
    uint32_t fn_core, uint16_t flow_group)
{
  const struct eth_hdr *eth = buf;
  const struct ip_hdr *ip = (struct ip_hdr *) (eth + 1);
  const struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
  int to_kni = 1;

  if (f_beui16(eth->type) == ETH_TYPE_ARP) {
    if (len < sizeof(struct pkt_arp)) {
      fprintf(stderr, "process_packet: short arp packet\n");
      return;
    }

    arp_packet(buf, len);
  } else if (f_beui16(eth->type) == ETH_TYPE_IP) {
    if (len < sizeof(*eth) + sizeof(*ip)) {
      fprintf(stderr, "process_packet: short ip packet\n");
      return;
    }

    if (ip->proto == IP_PROTO_TCP) {
      if (len < sizeof(*eth) + sizeof(*ip) + sizeof(*tcp)) {
        fprintf(stderr, "process_packet: short tcp packet\n");
        return;
      }

      to_kni = !!tcp_packet(buf, len, fn_core, flow_group);
    }
  }

  if (to_kni)
    kni_packet(buf, len);
}

static inline volatile struct flextcp_pl_ktx *ktx_try_alloc(uint32_t core,
    struct nic_buffer **pbuf, uint32_t *new_tail)
{
  uint32_t tail = txq_tail[core];
  volatile struct flextcp_pl_ktx *ktx = &txq_base[core][tail];
  struct nic_buffer *buf = &txq_bufs[core][tail];

  /* queue is full */
  if (ktx->type != 0) {
    return NULL;
  }

  /* update tail */
  tail = tail + 1;
  if (tail == rxq_len) {
    tail -= rxq_len;
  }

  *pbuf = buf;
  *new_tail = tail;

  return ktx;
}

static inline uint32_t flow_hash(ip_addr_t lip, beui16_t lp,
    ip_addr_t rip, beui16_t rp)
{
  struct {
    ip_addr_t lip;
    ip_addr_t rip;
    beui16_t lp;
    beui16_t rp;
  } __attribute__((packed)) hk =
      { .lip = lip, .rip = rip, .lp = lp, .rp = rp };
  MEM_BARRIER();
  return rte_hash_crc(&hk, sizeof(hk), 0);
}

static inline int flow_slot_alloc(uint32_t h, uint32_t *pi, uint32_t *pd)
{
  uint32_t j, i, l, k, d;
  struct flextcp_pl_flowhte *hte = fp_state->flowht;

  /* find slot */
  j = h % FLEXNIC_PL_FLOWHT_ENTRIES;
  l = (j + FLEXNIC_PL_FLOWHT_NBSZ) % FLEXNIC_PL_FLOWHT_ENTRIES;

  /* look for empty slot */
  d = 0;
  for (i = j; i != l; i = (i + 1) % FLEXNIC_PL_FLOWHT_ENTRIES) {
    if ((hte[i].flow_id & FLEXNIC_PL_FLOWHTE_VALID) == 0) {
      *pi = i;
      *pd = d;
      return 0;
    }
    d++;
  }

  /* no free slot, try to clear up on */
  k = (l + 4 * FLEXNIC_PL_FLOWHT_NBSZ) % FLEXNIC_PL_FLOWHT_ENTRIES;
  /* looking for candidate empty slot to move back */
  for (; i != k; i = (i + 1) % FLEXNIC_PL_FLOWHT_ENTRIES) {
    if ((hte[i].flow_id & FLEXNIC_PL_FLOWHTE_VALID) == 0) {
      break;
    }
  }

  /* abort if no candidate slot found */
  if (i == k) {
    fprintf(stderr, "flow_slot_alloc: no empty slot found\n");
    return -1;
  }

  /* move candidate backwards until in range for this insertion */
  /* j < l -> (i < j || i >= l) */
  /* j > l -> (i >= l && i < j) */
  while ((j > l || (i < j || i >= l)) && (j < l || (i >= l && i < j))) {
    k = i;

    /* look for element to swap */
    i = (k - FLEXNIC_PL_FLOWHT_NBSZ) % FLEXNIC_PL_FLOWHT_ENTRIES;
    for (; i != k; i = (i + 1) % FLEXNIC_PL_FLOWHT_ENTRIES) {
      assert((hte[i].flow_id & FLEXNIC_PL_FLOWHTE_VALID) != 0);

      /* calculate how much further this element can be moved */
      d = (hte[i].flow_id >> FLEXNIC_PL_FLOWHTE_POSSHIFT) &
          (FLEXNIC_PL_FLOWHT_NBSZ - 1);
      d = FLEXNIC_PL_FLOWHT_NBSZ - 1 - d;

      /* check whether element can be moved */
      if ((k - i) % FLEXNIC_PL_FLOWHT_ENTRIES <= d) {
        break;
      }
    }

    /* abort if none of the elements can be moved */
    if (i == k) {
      fprintf(stderr, "flow_slot_alloc: no element could be moved\n");
      return -1;
    }

    /* move element up */
    assert((hte[k].flow_id & FLEXNIC_PL_FLOWHTE_VALID) == 0);
    d = (hte[i].flow_id >> FLEXNIC_PL_FLOWHTE_POSSHIFT) &
        (FLEXNIC_PL_FLOWHT_NBSZ - 1);

    /* write to empty entry first */
    hte[k].flow_hash = hte[i].flow_hash;
    MEM_BARRIER();
    hte[k].flow_id = FLEXNIC_PL_FLOWHTE_VALID |
        (d << FLEXNIC_PL_FLOWHTE_POSSHIFT) |
        (((1 << FLEXNIC_PL_FLOWHTE_POSSHIFT) - 1) & hte[i].flow_id);
    MEM_BARRIER();

    /* empty original position */
    hte[i].flow_id = 0;
    MEM_BARRIER();
  }

  *pi = i;
  *pd = (i - j) % FLEXNIC_PL_FLOWHT_ENTRIES;
  return 0;
}

static inline int flow_slot_clear(uint32_t f_id, ip_addr_t lip, beui16_t lp,
    ip_addr_t rip, beui16_t rp)
{
  uint32_t h, k, j, ffid, eh;
  struct flextcp_pl_flowhte *e;

  h = flow_hash(lip, lp, rip, rp);

  for (j = 0; j < FLEXNIC_PL_FLOWHT_NBSZ; j++) {
    k = (h + j) % FLEXNIC_PL_FLOWHT_ENTRIES;
    e = &fp_state->flowht[k];

    ffid = e->flow_id;
    MEM_BARRIER();
    eh = e->flow_hash;

    if ((ffid & FLEXNIC_PL_FLOWHTE_VALID) == 0 || eh != h) {
      continue;
    }

    if ((ffid & ((1 << FLEXNIC_PL_FLOWHTE_POSSHIFT) - 1)) == f_id) {
      e->flow_id &= ~FLEXNIC_PL_FLOWHTE_VALID;
      return 0;
    }
  }

  fprintf(stderr, "flow_slot_clear: table entry not found\n");
  return -1;
}

static void flow_id_alloc_init(void)
{
  size_t i;
  struct flow_id_item *it, *prev = NULL;
  for (i = 0; i < FLEXNIC_PL_FLOWST_NUM; i++) {
    it = &flow_id_items[i];
    it->flow_id = i;
    it->next = NULL;

    if (prev == NULL) {
      flow_id_freelist = it;
    } else {
      prev->next = it;
    }
    prev = it;
  }
}

static int flow_id_alloc(uint32_t *fid)
{
  struct flow_id_item *it = flow_id_freelist;

  if (it == NULL)
    return -1;

  flow_id_freelist = it->next;
  *fid = it->flow_id;
  return 0;
}

static void flow_id_free(uint32_t flow_id)
{
  struct flow_id_item *it = &flow_id_items[flow_id];
  it->next = flow_id_freelist;
  flow_id_freelist = it;
}
