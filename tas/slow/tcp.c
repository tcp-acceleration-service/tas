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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <rte_config.h>
#include <rte_ip.h>
#include <rte_hash_crc.h>

#include <tas.h>
#include <packet_defs.h>
#include <utils.h>
#include <utils_rng.h>
#include "internal.h"

#define TCP_MSS 1460
#define TCP_HTSIZE 4096

#define PORT_MAX ((1u << 16) - 1)
#define PORT_FIRST_EPH 8192

#define PORT_TYPE_UNUSED 0x0ULL
#define PORT_TYPE_LISTEN 0x1ULL
#define PORT_TYPE_LMULTI 0x2ULL
#define PORT_TYPE_CONN   0x3ULL
#define PORT_TYPE_MASK   0x3ULL

/* maximum number of listening sockets per port */
#define LISTEN_MULTI_MAX 32

#define CONN_DEBUG(c, f, x...) do { } while (0)
#define CONN_DEBUG0(c, f) do { } while (0)
/*#define CONN_DEBUG(c, f, x...) fprintf(stderr, "conn(%p): " f, c, x)
#define CONN_DEBUG0(c, f, x...) fprintf(stderr, "conn(%p): " f, c)*/

struct listen_multi {
  size_t num;
  struct listener *ls[LISTEN_MULTI_MAX];
};

struct backlog_slot {
  uint8_t buf[126];
  uint16_t len;
};

struct tcp_opts {
  struct tcp_mss_opt *mss;
  struct tcp_ws_opt *ws;
  struct tcp_timestamp_opt *ts;
};

static int conn_arp_done(struct connection *conn);
static void conn_packet(struct connection *c, const struct pkt_tcp *p,
    const struct tcp_opts *opts, uint32_t fn_core, uint16_t flow_group);
static inline struct connection *conn_alloc(void);
static inline void conn_free(struct connection *conn);
static void conn_register(struct connection *conn);
static void conn_unregister(struct connection *conn);
static struct connection *conn_lookup(const struct pkt_tcp *p);
static int conn_syn_sent_packet(struct connection *c, const struct pkt_tcp *p,
    const struct tcp_opts *opts);
static int conn_reg_synack(struct connection *c);
static void conn_failed(struct connection *c, int status);
static void conn_timeout_arm(struct connection *c, int type);
static void conn_timeout_disarm(struct connection *c);
static void conn_close_timeout(struct connection *c);

static struct listener *listener_lookup(const struct pkt_tcp *p);
static void listener_packet(struct listener *l, const struct pkt_tcp *p,
    const struct tcp_opts *opts, uint32_t fn_core, uint16_t flow_group);
static void listener_accept(struct listener *l);

static inline uint16_t port_alloc(void);
static inline int send_control(const struct connection *conn, uint16_t flags,
    int ts_opt, uint32_t ts_echo, uint16_t mss_opt, uint8_t ws_opt);
static inline int send_reset(const struct pkt_tcp *p,
    const struct tcp_opts *opts);
static inline int parse_options(const struct pkt_tcp *p, uint16_t len,
    struct tcp_opts *opts);

static uintptr_t ports[PORT_MAX + 1];
static uint16_t port_eph_hint = PORT_FIRST_EPH;
static struct nbqueue conn_async_q;
struct connection **tcp_hashtable = NULL;
static struct utils_rng rng;

int tcp_init(void)
{
  nbqueue_init(&conn_async_q);
  utils_rng_init(&rng, util_timeout_time_us());

  port_eph_hint = utils_rng_gen32(&rng) % ((1 << 16) - 1 - PORT_FIRST_EPH);
  if ((tcp_hashtable = calloc(TCP_HTSIZE, sizeof(*tcp_hashtable))) == NULL) {
    return -1;
  }
  return 0;
}

void tcp_poll(void)
{
  struct connection *conn;
  uint8_t *p;
  int ret;

  while ((p = nbqueue_deq(&conn_async_q)) != NULL) {
    conn = (struct connection *) (p - offsetof(struct connection, comp.el));
    if (conn->status == CONN_ARP_PENDING) {
      if ((ret = conn->comp.status) != 0 || (ret = conn_arp_done(conn)) != 0) {
        conn_failed(conn, ret);
      }
    } else if (conn->status == CONN_REG_SYNACK) {
      if ((ret = conn->comp.status) != 0 ||
          (ret = conn_reg_synack(conn)) != 0)
      {
        conn_failed(conn, ret);
      }
    } else {
      fprintf(stderr, "tcp_poll: unexpected conn state %u\n", conn->status);
    }
  }
}

int tcp_open(struct app_context *ctx, uint64_t opaque, uint32_t remote_ip,
    uint16_t remote_port, uint32_t db_id, struct connection **pconn)
{
  int ret;
  struct connection *conn;
  uint16_t local_port;

  /* allocate connection struct */
  if ((conn = conn_alloc()) == NULL) {
    fprintf(stderr, "tcp_open: malloc failed\n");
    return -1;
  }

  CONN_DEBUG(conn, "opening connection (ctx=%p, op=%"PRIx64", rip=%x, rp=%u, "
      "db=%u)\n", ctx, opaque, remote_ip, remote_port, db_id);

  /* allocate local port */
  if ((local_port = port_alloc()) == 0) {
    fprintf(stderr, "tcp_open: port_alloc failed\n");
    conn_free(conn);
    return -1;
  }

  conn->ctx = ctx;
  conn->opaque = opaque;
  conn->status = CONN_ARP_PENDING;
  conn->remote_ip = remote_ip;
  conn->local_ip = config.ip;
  conn->remote_port = remote_port;
  conn->local_port = local_port;
  conn->local_seq = 0; /* TODO: assign random */
  conn->remote_seq = 0;
  conn->cnt_tx_pending = 0;
  conn->db_id = db_id;
  conn->flags = 0;

  conn->comp.q = &conn_async_q;
  conn->comp.notify_fd = -1;
  conn->comp.status = 0;


  /* resolve IP to mac */
  ret = routing_resolve(&conn->comp, remote_ip, &conn->remote_mac);
  if (ret < 0) {
    fprintf(stderr, "tcp_open: nicif_arp failed\n");
    conn_free(conn);
    return -1;
  } else if (ret == 0) {
    CONN_DEBUG0(conn, "routing_resolve succeeded immediately\n");
    conn_register(conn);

    ret = conn_arp_done(conn);
  } else {
    CONN_DEBUG0(conn, "routing_resolve pending\n");
    conn_register(conn);
    ret = 0;
  }

  ports[local_port] = (uintptr_t) conn | PORT_TYPE_CONN;

  *pconn = conn;
  return ret;
}

int tcp_listen(struct app_context *ctx, uint64_t opaque, uint16_t local_port,
    uint32_t backlog, int reuseport, struct listener **listen)
{
  struct listener *lst;
  uint32_t i;
  struct backlog_slot *bls;
  struct listen_multi *lm = NULL, *lm_new = NULL;
  uint8_t type;

  /* make sure port is unused */
  type = ports[local_port] & PORT_TYPE_MASK;
  if (type != PORT_TYPE_UNUSED && reuseport == 0) {
    fprintf(stderr, "tcp_listen: port not unused\n");
    return -1;
  } else if (reuseport != 0 && type != PORT_TYPE_UNUSED &&
      type != PORT_TYPE_LMULTI)
  {
    fprintf(stderr, "tcp_listen: port not unused or multi listener\n");
    return -1;
  }

  /* allocate listen_multi if required */
  if (reuseport != 0 && type == PORT_TYPE_UNUSED) {
    if ((lm_new = calloc(1, sizeof(*lm_new))) == NULL) {
      fprintf(stderr, "tcp_listen: calloc listen_multi failed\n");
      return -1;
    }
    lm = lm_new;
  } else if (reuseport != 0) {
    lm = (struct listen_multi *) (ports[local_port] & ~PORT_TYPE_MASK);
    if (lm->num == LISTEN_MULTI_MAX) {
      fprintf(stderr, "tcp_listen: no more additional listeners supported\n");
      return -1;
    }
  }

  /* allocate listener struct */
  if ((lst = calloc(1, sizeof(*lst))) == NULL) {
    fprintf(stderr, "tcp_listen: malloc failed\n");
    free(lm_new);
    return -1;
  }

  /* allocate backlog queue */
  if ((lst->backlog_ptrs = calloc(backlog, sizeof(void *))) == NULL) {
    fprintf(stderr, "tcp_listen: malloc backlog_ptrss failed\n");
    free(lst);
    free(lm_new);
    return -1;
  }
  if ((lst->backlog_cores = calloc(backlog, sizeof(*lst->backlog_cores))) ==
      NULL)
  {
    fprintf(stderr, "tcp_listen: malloc backlog_cores failed\n");
    free(lst->backlog_ptrs);
    free(lst);
    free(lm_new);
    return -1;
  }
  if ((lst->backlog_fgs = calloc(backlog, sizeof(*lst->backlog_fgs))) ==
      NULL)
  {
    fprintf(stderr, "tcp_listen: malloc backlog_fgs failed\n");
    free(lst->backlog_cores);
    free(lst->backlog_ptrs);
    free(lst);
    free(lm_new);
    return -1;
  }

  /* allocate backlog buffers */
  if ((bls = malloc(sizeof(*bls) * backlog)) == NULL) {
    fprintf(stderr, "tcp_listen: malloc backlog bufs failed\n");
    free(lst->backlog_fgs);
    free(lst->backlog_cores);
    free(lst->backlog_ptrs);
    free(lst);
    free(lm_new);
    return -1;
  }
  for (i = 0; i < backlog; i++) {
    lst->backlog_ptrs[i] = &bls[i];
  }

  /* initialize listener */
  lst->ctx = ctx;
  lst->opaque = opaque;
  lst->port = local_port;
  lst->wait_conns = NULL;
  lst->backlog_len = backlog;
  lst->backlog_pos = 0;
  lst->backlog_used = 0;
  lst->flags = 0;

  /* add to port tables */
  if (reuseport == 0) {
    ports[local_port] = (uintptr_t) lst | PORT_TYPE_LISTEN;
  } else {
    lm->ls[lm->num] = lst;
    lm->num++;
    if (lm_new != NULL) {
      lm = lm_new;
      ports[local_port] = (uintptr_t) lm | PORT_TYPE_LMULTI;
    }
  }

  *listen = lst;

  return 0;
}

int tcp_accept(struct app_context *ctx, uint64_t opaque,
    struct listener *listen, uint32_t db_id)
{
  struct connection *conn;

  /* allocate listener struct */
  if ((conn = conn_alloc()) == NULL) {
    fprintf(stderr, "tcp_accept: conn_alloc failed\n");
    return -1;
  }

  conn->ctx = ctx;
  conn->opaque = opaque;
  conn->status = CONN_SYN_WAIT;
  conn->local_port = listen->port;
  conn->db_id = db_id;
  conn->flags = listen->flags;
  conn->cnt_tx_pending = 0;

  conn->ht_next = listen->wait_conns;
  listen->wait_conns = conn;

  if (listen->backlog_used > 0) {
    listener_accept(listen);
  }
  return 0;
}

int tcp_packet(const void *pkt, uint16_t len, uint32_t fn_core,
    uint16_t flow_group)
{
  struct connection *c;
  struct listener *l;
  const struct pkt_tcp *p = pkt;
  struct tcp_opts opts;
  int ret = 0;

  if (len < sizeof(*p)) {
    fprintf(stderr, "tcp_packet: incomplete TCP receive (%u received, "
        "%u expected)\n", len, (unsigned) sizeof(*p));
    return -1;
  }

  if (f_beui32(p->ip.dest) != config.ip) {
    fprintf(stderr, "tcp_packet: unexpected destination IP (%x received, "
        "%x expected)\n", f_beui32(p->ip.dest), config.ip);
    return -1;
  }

  if (parse_options(p, len, &opts) != 0) {
    fprintf(stderr, "tcp_packet: parsing TCP options failed\n");
    return -1;
  }

  if ((c = conn_lookup(p)) != NULL) {
    conn_packet(c, p, &opts, fn_core, flow_group);
  } else if ((l = listener_lookup(p)) != NULL) {
    listener_packet(l, p, &opts, fn_core, flow_group);
  } else {
    ret = -1;

    /* send reset if the packet received wasn't a reset */
    if (!(TCPH_FLAGS(&p->tcp) & TCP_RST) &&
        config.kni_name == NULL)
      send_reset(p, &opts);
  }

  return ret;
}

int tcp_close(struct connection *conn)
{
  uint32_t tx_seq, rx_seq;
  int tx_c, rx_c;

  if (conn->status != CONN_OPEN) {
    fprintf(stderr, "tcp_close: currently no support for non-opened conns.\n");
    return -1;
  }

  /* disable connection on fastpath */
  if (nicif_connection_disable(conn->flow_id, &tx_seq, &rx_seq, &tx_c, &rx_c)
      != 0)
  {
    fprintf(stderr, "tcp_close: nicif_connection_disable failed unexpected\n");
    return -1;
  }

  conn->remote_seq = rx_seq;
  conn->local_seq = tx_seq;

  if (!tx_c || !rx_c) {
    send_control(conn, TCP_RST, 0, 0, 0, 0);
  }

  cc_conn_remove(conn);

  conn->status = CONN_CLOSED;

  /* set timer to free connection state */
  assert(conn->to_armed == 0);
  util_timeout_arm(&timeout_mgr, &conn->to, 10000, TO_TCP_CLOSED);
  conn->to_armed = 1;
  return 0;
}

void tcp_destroy(struct connection *conn)
{
  assert(conn->status == CONN_FAILED);
  conn_free(conn);
}

void tcp_timeout(struct timeout *to, enum timeout_type type)
{
  struct connection *c = (struct connection *)
    ((uintptr_t) to - offsetof(struct connection, to));

  assert(c->to_armed);
  c->to_armed = 0;

  /* validate type and connection state */
  if (type == TO_TCP_CLOSED) {
    conn_close_timeout(c);
    return;
  } else if (type != TO_TCP_HANDSHAKE) {
    fprintf(stderr, "tcp_timeout: unexpected timeout type (%u)\n", type);
    abort();
  }
  if (c->status != CONN_SYN_SENT) {
    fprintf(stderr, "tcp_timeout: unexpected connection state (%u)\n", c->status);
    abort();
  }

  /* close connection if too many retries */
  if (++c->to_attempts > config.tcp_handshake_retries) {
    fprintf(stderr, "tcp_timeout: giving up because of too many retries\n");
    conn_failed(c, -1);
    return;
  }

  /* re-arm timeout */
  c->timeout *= 2;
  conn_timeout_arm(c, TO_TCP_HANDSHAKE);

  /* re-send SYN packet */
  send_control(c, TCP_SYN | TCP_ECE | TCP_CWR, 1, 0, TCP_MSS, c->rx_window_scale);
}

static void conn_packet(struct connection *c, const struct pkt_tcp *p,
    const struct tcp_opts *opts, uint32_t fn_core, uint16_t flow_group)
{
  int ret;
  uint32_t ecn_flags = 0;

  if (c->status == CONN_SYN_SENT) {
    /* hopefully a SYN-ACK received */
    c->fn_core = fn_core;
    c->flow_group = flow_group;
    if ((ret = conn_syn_sent_packet(c, p, opts)) != 0) {
      conn_failed(c, ret);
    }
  } else if (c->status == CONN_OPEN &&
      (TCPH_FLAGS(&p->tcp) & ~ecn_flags) == TCP_SYN)
  {
    /* handle re-transmitted SYN for dropped SYN-ACK */
    /* TODO: should only do this if we're still waiting for initial ACK,
     * otherwise we should send a challenge ACK */
    if (opts->ts == NULL) {
      fprintf(stderr, "conn_packet: re-transmitted SYN does not have TS "
          "option\n");
      conn_failed(c, -1);
      return;
    }

    /* send ECN accepting SYN-ACK */
    if ((c->flags & NICIF_CONN_ECN) == NICIF_CONN_ECN) {
      ecn_flags = TCP_ECE;
    }

    send_control(c, TCP_SYN | TCP_ACK | ecn_flags, 1,
        f_beui32(opts->ts->ts_val), TCP_MSS, c->rx_window_scale);
  } else if (c->status == CONN_OPEN &&
      (TCPH_FLAGS(&p->tcp) & TCP_SYN) == TCP_SYN)
  {
    /* silently ignore a re-transmited SYN_ACK */
  } else if (c->status == CONN_CLOSED &&
      (TCPH_FLAGS(&p->tcp) & TCP_FIN) == TCP_FIN)
  {
   /* silently ignore a FIN for an already closed connection: TODO figure out
    * why necessary*/
    send_control(c, TCP_ACK, 1, 0, 0, 0);
  } else {
    fprintf(stderr, "tcp_packet: unexpected connection state %u\n", c->status);
  }
}

static int conn_arp_done(struct connection *conn)
{
  CONN_DEBUG0(conn, "arp resolution done\n");

  conn->status = CONN_SYN_SENT;

  /* arm timeout */
  conn->to_attempts = 0;
  conn->timeout = config.tcp_handshake_to;
  conn_timeout_arm(conn, TO_TCP_HANDSHAKE);

  /* send SYN */
  send_control(conn, TCP_SYN | TCP_ECE | TCP_CWR, 1, 0, TCP_MSS, conn->rx_window_scale);

  CONN_DEBUG0(conn, "SYN SENT\n");
  return 0;
}

static int conn_syn_sent_packet(struct connection *c, const struct pkt_tcp *p,
    const struct tcp_opts *opts)
{
  uint32_t ecn_flags = TCPH_FLAGS(&p->tcp) & (TCP_ECE | TCP_CWR);

  /* dis-arm timeout */
  conn_timeout_disarm(c);

  if ((TCPH_FLAGS(&p->tcp) & (TCP_SYN | TCP_ACK)) != (TCP_SYN | TCP_ACK)) {
    fprintf(stderr, "conn_syn_sent_packet: unexpected flags %x\n",
        TCPH_FLAGS(&p->tcp));
    return -1;
  }
  if (opts->ts == NULL) {
    fprintf(stderr, "conn_syn_sent_packet: no timestamp option received\n");
    return -1;
  }

  CONN_DEBUG0(c, "conn_syn_sent_packet: syn-ack received\n");

  c->remote_seq = f_beui32(p->tcp.seqno) + 1;
  c->local_seq = f_beui32(p->tcp.ackno);
  c->syn_ts = f_beui32(opts->ts->ts_val);

  if (opts->ws == NULL)
    c->tx_window_scale = 0;
  else
    c->tx_window_scale = opts->ws->scale;

  /* enable ECN if SYN-ACK confirms */
  if (ecn_flags == TCP_ECE) {
    c->flags |= NICIF_CONN_ECN;
  }

  cc_conn_init(c);

  c->comp.q = &conn_async_q;
  c->comp.notify_fd = -1;
  c->comp.status = 0;

  if (nicif_connection_add(c->db_id, c->remote_mac, c->local_ip, c->local_port,
        c->remote_ip, c->remote_port, c->rx_buf - (uint8_t *) tas_shm,
        c->rx_len, c->tx_buf - (uint8_t *) tas_shm, c->tx_len,
        c->remote_seq, c->local_seq, c->opaque, c->flags, c->cc_rate,
        c->rx_window_scale, c->tx_window_scale,
        c->fn_core, c->flow_group, &c->flow_id)
      != 0)
  {
    fprintf(stderr, "conn_syn_sent_packet: nicif_connection_add failed\n");
    return -1;
  }

  CONN_DEBUG0(c, "conn_syn_sent_packet: connection registered\n");

  c->status = CONN_OPEN;

  /* send ACK */
  send_control(c, TCP_ACK, 1, c->syn_ts, 0, 0);

  CONN_DEBUG0(c, "conn_syn_sent_packet: ACK sent\n");

  appif_conn_opened(c, 0);

  return 0;
}

static int conn_reg_synack(struct connection *c)
{
  uint32_t ecn_flags = 0;

  c->status = CONN_OPEN;

  if ((c->flags & NICIF_CONN_ECN) == NICIF_CONN_ECN) {
    ecn_flags = TCP_ECE;
  }

  /* send ACK */
  send_control(c, TCP_SYN | TCP_ACK | ecn_flags, 1, c->syn_ts, TCP_MSS, c->rx_window_scale);

  appif_accept_conn(c, 0);

  return 0;
}

static inline uint16_t port_alloc(void)
{
  uint16_t p, p_start, p_next;

  p = p_start = port_eph_hint;
  do {
    p_next = (((uint16_t) (p + 1)) < (uint16_t) PORT_FIRST_EPH ?
        PORT_FIRST_EPH : p + 1);

    if ((ports[p] & PORT_TYPE_MASK) == PORT_TYPE_UNUSED) {
      port_eph_hint = p_next;
      return p;
    }

    p = p_next;
  } while (p != p_start);

  return 0;
}

static inline struct connection *conn_alloc(void)
{
  struct connection *conn;
  uintptr_t off_rx, off_tx;

  if ((conn = malloc(sizeof(*conn))) == NULL) {
    fprintf(stderr, "conn_alloc: malloc failed\n");
    return NULL;
  }

  if (packetmem_alloc(config.tcp_rxbuf_len, &off_rx, &conn->rx_handle) != 0) {
    fprintf(stderr, "conn_alloc: packetmem_alloc rx failed\n");
    free(conn);
    return NULL;
  }

  if (packetmem_alloc(config.tcp_txbuf_len, &off_tx, &conn->tx_handle) != 0) {
    fprintf(stderr, "conn_alloc: packetmem_alloc tx failed\n");
    packetmem_free(conn->rx_handle);
    free(conn);
    return NULL;
  }

  conn->rx_buf = (uint8_t *) tas_shm + off_rx;
  conn->rx_len = config.tcp_rxbuf_len;
  conn->tx_buf = (uint8_t *) tas_shm + off_tx;
  conn->tx_len = config.tcp_txbuf_len;
  conn->rx_window_scale = config.tcp_window_scale;
  conn->to_armed = 0;

  return conn;
}

static inline void conn_free(struct connection *conn)
{
  packetmem_free(conn->tx_handle);
  packetmem_free(conn->rx_handle);
  free(conn);
}

static inline uint32_t conn_hash(uint32_t l_ip, uint32_t r_ip, uint16_t l_po,
    uint16_t r_po)
{
  return crc32c_sse42_u32(l_po | (((uint32_t) r_po) << 16),
      crc32c_sse42_u64(l_ip | (((uint64_t) r_ip) << 32), 0));
}

static void conn_register(struct connection *conn)
{
  uint32_t h;

  h = conn_hash(conn->local_ip, conn->remote_ip, conn->local_port,
      conn->remote_port) % TCP_HTSIZE;

  conn->ht_next = tcp_hashtable[h];
  tcp_hashtable[h] = conn;
}

static void conn_unregister(struct connection *conn)
{
  struct connection *cp = NULL;
  uint32_t h;

  h = conn_hash(conn->local_ip, conn->remote_ip, conn->local_port,
      conn->remote_port) % TCP_HTSIZE;
  if (tcp_hashtable[h] == conn) {
    tcp_hashtable[h] = conn->ht_next;
  } else {
    for (cp = tcp_hashtable[h]; cp != NULL && cp->ht_next != conn;
        cp = cp->ht_next);
    if (cp == NULL) {
      fprintf(stderr, "conn_unregister: connection not found in ht\n");
      abort();
    }

    cp->ht_next = conn->ht_next;
  }
}

static struct connection *conn_lookup(const struct pkt_tcp *p)
{
  uint32_t h;
  struct connection *c;

  h = conn_hash(f_beui32(p->ip.dest), f_beui32(p->ip.src),
      f_beui16(p->tcp.dest), f_beui16(p->tcp.src)) % TCP_HTSIZE;

  for (c = tcp_hashtable[h]; c != NULL; c = c->ht_next) {
    if (f_beui32(p->ip.src) == c->remote_ip &&
        f_beui16(p->tcp.dest) == c->local_port &&
        f_beui16(p->tcp.src) == c->remote_port)
    {
      return c;
    }
  }
  return NULL;
}

static void conn_failed(struct connection *c, int status)
{
  conn_unregister(c);
  if (c->to_armed) {
    conn_timeout_disarm(c);
  }

  c->status = CONN_FAILED;

  appif_conn_opened(c, status);
}

static void conn_timeout_arm(struct connection *c, int type)
{
  uint32_t to;

  assert(!c->to_armed);
  c->to_armed = 1;

  /* randomize timeout +/- 50% to avoid thundering herds */
  to = c->timeout / 2 + (utils_rng_gen32(&rng) % c->timeout);
  util_timeout_arm(&timeout_mgr, &c->to, to, TO_TCP_HANDSHAKE);
}

static void conn_timeout_disarm(struct connection *c)
{
  assert(c->to_armed);
  c->to_armed = 0;

  util_timeout_disarm(&timeout_mgr, &c->to);
}

static void conn_close_timeout(struct connection *c)
{
  /* free port */
  if ((ports[c->local_port] & PORT_TYPE_MASK) == PORT_TYPE_CONN) {
    ports[c->local_port] = 0;
  }

  /* remove from global connection list */
  conn_unregister(c);

  /* free connection data buffers */
  packetmem_free(c->tx_handle);
  packetmem_free(c->rx_handle);

  /* free connection id */
  nicif_connection_free(c->flow_id);

  /* free ephemeral port */
  if ((ports[c->local_port] & PORT_TYPE_MASK) == PORT_TYPE_CONN) {
    ports[c->local_port] = PORT_TYPE_UNUSED;
  }

  /* notify application */
  appif_conn_closed(c, 0);

  free(c);
}

/** simple hash of 64-bits to 32 bits */
static inline uint32_t hash_64_to_32(uint64_t key)
{
  key = (~key) + (key << 18);
  key = key ^ (key >> 31);
  key = key * 21;
  key = key ^ (key >> 11);
  key = key + (key << 6);
  key = key ^ (key >> 22);
  return (uint32_t) key;
}

static struct listener *listener_lookup(const struct pkt_tcp *p)
{
  uint16_t local_port = f_beui16(p->tcp.dest);
  uint32_t hash;
  uint8_t type;
  struct listen_multi *lm;

  type = ports[local_port] & PORT_TYPE_MASK;
  if (type == PORT_TYPE_LISTEN) {
    /* single listener socket */
    return (struct listener *) (ports[local_port] & ~PORT_TYPE_MASK);
  } else if (type == PORT_TYPE_LMULTI) {
    /* multiple listener sockets, calculate hash */
    lm = (struct listen_multi *) (ports[local_port] & ~PORT_TYPE_MASK);
    hash = hash_64_to_32(((uint64_t) f_beui32(p->ip.src) << 32) |
        ((uint32_t) f_beui16(p->tcp.src) << 16) | local_port);
    return lm->ls[hash % lm->num];
  } else {
    return NULL;
  }

  return (struct listener *) (ports[local_port] & ~PORT_TYPE_MASK);
}

static void listener_packet(struct listener *l, const struct pkt_tcp *p,
    const struct tcp_opts *opts, uint32_t fn_core, uint16_t flow_group)
{
  struct backlog_slot *bls;
  uint16_t len;
  uint32_t bp, n;
  struct pkt_tcp *bl_p;

  if ((TCPH_FLAGS(&p->tcp) & ~(TCP_ECE | TCP_CWR)) != TCP_SYN) {
    fprintf(stderr, "listener_packet: Not a SYN (flags %x)\n",
            TCPH_FLAGS(&p->tcp));
    send_reset(p, opts);
    return;
  }

  /* make sure packet is not too long */
  len = sizeof(p->eth) + f_beui16(p->ip.len);
  if (len > sizeof(bls->buf)) {
    fprintf(stderr, "listener_packet: SYN larger than backlog buffer, "
        "dropping\n");
    return;
  }

  /* make sure we don't already have this 4-tuple */
  for (n = 0, bp = l->backlog_pos; n < l->backlog_used;
      n++, bp = (bp + 1) % l->backlog_len)
  {
    bls = l->backlog_ptrs[bp];
    bl_p = (struct pkt_tcp *) bls->buf;
    if (f_beui32(p->ip.src) == f_beui32(bl_p->ip.src) &&
        f_beui32(p->ip.dest) == f_beui32(bl_p->ip.dest) &&
        f_beui16(p->tcp.src) == f_beui16(bl_p->tcp.src) &&
        f_beui16(p->tcp.dest) == f_beui16(bl_p->tcp.dest))
    {
      return;
    }
  }

  if (l->backlog_len == l->backlog_used) {
    fprintf(stderr, "listener_packet: backlog queue full\n");
    return;
  }


  bp = l->backlog_pos + l->backlog_used;
  if (bp >= l->backlog_len) {
    bp -= l->backlog_len;
  }

  /* copy packet into backlog buffer */
  l->backlog_cores[bp] = fn_core;
  l->backlog_fgs[bp] = flow_group;
  bls = l->backlog_ptrs[bp];
  memcpy(bls->buf, p, len);
  bls->len = len;

  l->backlog_used++;

  appif_listen_newconn(l, f_beui32(p->ip.src), f_beui16(p->tcp.src));

  /* check if there are pending accepts */
  if (l->wait_conns != NULL) {
    listener_accept(l);
  }
}

static void listener_accept(struct listener *l)
{
  struct connection *c = l->wait_conns;
  struct backlog_slot *bls;
  const struct pkt_tcp *p;
  struct tcp_opts opts;
  uint32_t ecn_flags, fn_core;
  uint16_t flow_group;
  int ret = 0;

  assert(c != NULL);
  assert(l->backlog_used > 0);

  bls = l->backlog_ptrs[l->backlog_pos];
  fn_core = l->backlog_cores[l->backlog_pos];
  flow_group = l->backlog_fgs[l->backlog_pos];
  p = (const struct pkt_tcp *) bls->buf;
  ret = parse_options(p, bls->len, &opts);
  if (ret != 0 || opts.ts == NULL) {
    fprintf(stderr, "listener_packet: parsing options failed or no timestamp "
        "option\n");
    goto out;
  }

  c->fn_core = fn_core;
  c->flow_group = flow_group;
  c->remote_mac = 0;
  memcpy(&c->remote_mac, &p->eth.src, ETH_ADDR_LEN);
  c->remote_ip = f_beui32(p->ip.src);
  c->local_ip = config.ip;
  c->remote_port = f_beui16(p->tcp.src);
  c->local_port = l->port;

  c->remote_seq = f_beui32(p->tcp.seqno) + 1;
  c->local_seq = 1; /* TODO: generate random */
  c->syn_ts = f_beui32(opts.ts->ts_val);
  if (opts.ws == NULL)
    c->tx_window_scale = 0;
  else
    c->tx_window_scale = opts.ws->scale;

  /* check if ECN is offered */
  ecn_flags = TCPH_FLAGS(&p->tcp) & (TCP_ECE | TCP_CWR);
  if (ecn_flags == (TCP_ECE | TCP_CWR)) {
    c->flags |= NICIF_CONN_ECN;
  }

  cc_conn_init(c);

  c->status = CONN_REG_SYNACK;

  c->comp.q = &conn_async_q;
  c->comp.notify_fd = -1;
  c->comp.status = 0;

  if (nicif_connection_add(c->db_id, c->remote_mac, c->local_ip, c->local_port,
        c->remote_ip, c->remote_port, c->rx_buf - (uint8_t *) tas_shm,
        c->rx_len, c->tx_buf - (uint8_t *) tas_shm, c->tx_len,
        c->remote_seq, c->local_seq + 1, c->opaque, c->flags, c->cc_rate,
        c->rx_window_scale, c->tx_window_scale,
        c->fn_core, c->flow_group, &c->flow_id)
      != 0)
  {
    fprintf(stderr, "listener_packet: nicif_connection_add failed\n");
    goto out;
  }

  l->wait_conns = c->ht_next;
  conn_register(c);
  nbqueue_enq(&conn_async_q, &c->comp.el);

out:
  l->backlog_used--;
  l->backlog_pos++;
  if (l->backlog_pos >= l->backlog_len) {
    l->backlog_pos -= l->backlog_len;
  }
}

static inline int send_control_raw(uint64_t remote_mac, uint32_t remote_ip,
    uint16_t remote_port, uint16_t local_port, uint32_t local_seq,
    uint32_t remote_seq, uint16_t flags, int ts_opt, uint32_t ts_echo,
    uint16_t mss_opt, uint8_t ws_opt)
{
  uint32_t new_tail;
  struct pkt_tcp *p;
  struct tcp_mss_opt *opt_mss;
  struct tcp_ws_opt *opt_ws;
  struct tcp_timestamp_opt *opt_ts;
  uint8_t optlen;
  uint16_t len, off_ts, off_ws, off_mss;

  /* calculate header length depending on options */
  optlen = 0;
  off_mss = optlen;
  optlen += (mss_opt ? sizeof(*opt_mss) : 0);
  off_ws = optlen;
  optlen += (ws_opt ? sizeof(*opt_ws) : 0);
  off_ts = optlen;
  optlen += (ts_opt ? sizeof(*opt_ts) : 0);
  optlen = (optlen + 3) & ~3;
  len = sizeof(*p) + optlen;

  /** allocate send buffer */
  if (nicif_tx_alloc(len, (void **) &p, &new_tail) != 0) {
    fprintf(stderr, "send_control failed\n");
    return -1;
  }

  /* fill ethernet header */
  memcpy(&p->eth.dest, &remote_mac, ETH_ADDR_LEN);
  memcpy(&p->eth.src, &eth_addr, ETH_ADDR_LEN);
  p->eth.type = t_beui16(ETH_TYPE_IP);

  /* fill ipv4 header */
  IPH_VHL_SET(&p->ip, 4, 5);
  p->ip._tos = 0;
  p->ip.len = t_beui16(len - offsetof(struct pkt_tcp, ip));
  p->ip.id = t_beui16(3); /* TODO: not sure why we have 3 here */
  p->ip.offset = t_beui16(0);
  p->ip.ttl = 0xff;
  p->ip.proto = IP_PROTO_TCP;
  p->ip.chksum = 0;
  p->ip.src = t_beui32(config.ip);
  p->ip.dest = t_beui32(remote_ip);

  /* fill tcp header */
  p->tcp.src = t_beui16(local_port);
  p->tcp.dest = t_beui16(remote_port);
  p->tcp.seqno = t_beui32(local_seq);
  p->tcp.ackno = t_beui32(remote_seq);
  TCPH_HDRLEN_FLAGS_SET(&p->tcp, 5 + optlen / 4, flags);
  p->tcp.wnd = t_beui16(11680); /* TODO */
  p->tcp.chksum = 0;
  p->tcp.urgp = t_beui16(0);

  /* if requested: add mss option */
  if (mss_opt) {
    opt_mss = (struct tcp_mss_opt *) ((uint8_t *) (p + 1) + off_mss);
    opt_mss->kind = TCP_OPT_MSS;
    opt_mss->length = sizeof(*opt_mss);
    opt_mss->mss = t_beui16(mss_opt);
  }

  /* if requested: add ws option */
  if (ws_opt) {
    opt_ws = (struct tcp_ws_opt *) ((uint8_t *) (p + 1) + off_ws);
    opt_ws->kind = TCP_OPT_WS;
    opt_ws->length = sizeof(*opt_ws);
    opt_ws->scale = ws_opt;
  }

  /* if requested: add timestamp option */
  if (ts_opt) {
    opt_ts = (struct tcp_timestamp_opt *) ((uint8_t *) (p + 1) + off_ts);
    memset(opt_ts, 0, optlen);
    opt_ts->kind = TCP_OPT_TIMESTAMP;
    opt_ts->length = sizeof(*opt_ts);
    opt_ts->ts_val = t_beui32(0);
    opt_ts->ts_ecr = t_beui32(ts_echo);
  }

  /* calculate header checksums */
  p->ip.chksum = rte_ipv4_cksum((void *) &p->ip);
  p->tcp.chksum = rte_ipv4_udptcp_cksum((void *) &p->ip, (void *) &p->tcp);
  
  /* send packet */
  nicif_tx_send(new_tail, 0);
  return 0;
}

static inline int send_control(const struct connection *conn, uint16_t flags,
    int ts_opt, uint32_t ts_echo, uint16_t mss_opt, uint8_t ws_opt)
{
  return send_control_raw(conn->remote_mac, conn->remote_ip, conn->remote_port,
      conn->local_port, conn->local_seq, conn->remote_seq, flags, ts_opt,
      ts_echo, mss_opt, ws_opt);
}

static inline int send_reset(const struct pkt_tcp *p,
    const struct tcp_opts *opts)
{
  int ts_opt = 0;
  uint32_t ts_val;
  uint64_t remote_mac = 0;

  if (opts->ts != NULL) {
    ts_opt = 1;
    ts_val = f_beui32(opts->ts->ts_val);
  }

  memcpy(&remote_mac, &p->eth.src, ETH_ADDR_LEN);
  return send_control_raw(remote_mac, f_beui32(p->ip.src), f_beui16(p->tcp.src),
      f_beui16(p->tcp.dest), f_beui32(p->tcp.ackno), f_beui32(p->tcp.seqno) + 1,
      TCP_RST | TCP_ACK, ts_opt, ts_val, 0, 0);
}

static inline int parse_options(const struct pkt_tcp *p, uint16_t len,
    struct tcp_opts *opts)
{
  uint8_t *opt = (uint8_t *) (p + 1);
  uint16_t opts_len = TCPH_HDRLEN(&p->tcp) * 4 - 20;
  uint16_t off = 0;
  uint8_t opt_kind, opt_len, opt_avail;

  opts->ts = NULL;
  opts->mss = NULL;

  /* whole header not in buf */
  if (TCPH_HDRLEN(&p->tcp) < 5 || opts_len > (len - sizeof(*p))) {
    fprintf(stderr, "hdrlen=%u opts_len=%u len=%u so=%zu\n", TCPH_HDRLEN(&p->tcp), opts_len, len, sizeof(*p));
    return -1;
  }

  while (off < opts_len) {
    opt_kind = opt[off];
    opt_avail = opts_len - off;
    if (opt_kind == TCP_OPT_END_OF_OPTIONS) {
      break;
    } else if (opt_kind == TCP_OPT_NO_OP) {
      opt_len = 1;
    } else {
      if (opt_avail < 2) {
        fprintf(stderr, "parse_options: opt_avail=%u kind=%u off=%u\n", opt_avail, opt_kind,  off);
        return -1;
      }

      opt_len = opt[off + 1];
      if (opt_kind == TCP_OPT_MSS) {
        if (opt_len != sizeof(struct tcp_mss_opt)) {
          fprintf(stderr, "parse_options: mss option size wrong (expect %zu "
              "got %u)\n", sizeof(struct tcp_mss_opt), opt_len);
          return -1;
        }

        opts->mss = (struct tcp_mss_opt *) (opt + off);
      } else if (opt_kind == TCP_OPT_WS) {
        if (opt_len != sizeof(struct tcp_ws_opt)) {
          fprintf(stderr, "parse_options: window scale option size wrong (expect %zu "
                "got %u)\n", sizeof(struct tcp_ws_opt), opt_len);
          return -1;
        }

        opts->ws = (struct tcp_ws_opt *) (opt + off);
      } else if (opt_kind == TCP_OPT_TIMESTAMP) {
        if (opt_len != sizeof(struct tcp_timestamp_opt)) {
          fprintf(stderr, "parse_options: opt_len=%u so=%zu\n", opt_len, sizeof(struct tcp_timestamp_opt));
          return -1;
        }

        opts->ts = (struct tcp_timestamp_opt *) (opt + off);
      }
    }
    off += opt_len;
  }

  return 0;
}
