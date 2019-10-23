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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>

#include <tas_trace.h>
#include <tas_memif.h>
#include <packet_defs.h>
#include <utils.h>


struct trace {
  struct flexnic_trace_header *hdr;
  void  *base;
  size_t len;
  size_t pos;
  uint32_t seq;
};


static inline void copy_from_pos(struct trace *t, size_t pos, size_t len,
    void *dst);
static struct trace *trace_connect(unsigned n);
static void trace_set_last(struct trace *t);
static int trace_prev(struct trace *t, void *buf, unsigned len, uint64_t *ts,
    uint16_t *type, uint32_t *seq);
static void event_dump(void *buf, size_t len, uint16_t type);
static void packet_dump(void *buf, size_t len);
static void dma_dump(void *buf, size_t len);
static void qmset_dump(void *buf, size_t len);
static void qmevt_dump(void *buf, size_t len);

int main(int argc, char *argv[])
{
  struct trace *t;
  static uint8_t buf[4096];
  uint64_t ts;
  uint16_t type;
  uint32_t seq;
  int ret;
  unsigned n = 0;

  if (argc >= 2) {
    n = atoi(argv[1]);
  }

  if ((t = trace_connect(n)) == NULL) {
    fprintf(stderr, "trace_connect failed\n");
    return EXIT_FAILURE;
  }

  trace_set_last(t);

  while ((ret = trace_prev(t, buf, sizeof(buf), &ts, &type, &seq)) >= 0) {
    printf("ts=%20"PRIu64"  seq=%u  type=%u:", ts, seq, type);
    event_dump(buf, ret, type);
  }

  return 0;
}

static struct trace *trace_connect(unsigned id)
{
  int fd;
  char name[64];
  void *m;
  struct trace *t;
  struct stat sb;
  snprintf(name, sizeof(name), FLEXNIC_TRACE_NAME, id);

  if ((t = malloc(sizeof(*t))) == NULL) {
    perror("trace_connect: malloc failed");
    return NULL;
  }

  if ((fd = shm_open(name, O_RDONLY, 0)) == -1) {
    free(t);
    return NULL;
  }

  if (fstat(fd, &sb) != 0) {
    perror("trace_connect: fstat failed");
    close(fd);
    free(t);
    return NULL;
  }

  m = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE,
        fd, 0);
  close(fd);
  if (m == (void *) -1) {
    perror("trace_connect: mmap failed");
    free(t);
  }

  t->hdr = m;
  t->base = t->hdr + 1;
  t->len = sb.st_size - sizeof(*t->hdr);
  t->pos = 0;
  return t;
}

static void trace_set_last(struct trace *t)
{
  t->pos = t->hdr->end_last;
}

static int trace_prev(struct trace *t, void *buf, unsigned len, uint64_t *ts,
    uint16_t *type, uint32_t *seq)
{
  struct flexnic_trace_entry_head teh;
  struct flexnic_trace_entry_tail tet;
  size_t tet_pos, data_pos, teh_pos;
  uint16_t data_len;

  /* copy tail */
  if (t->pos >= sizeof(tet)) {
    tet_pos = t->pos - sizeof(tet);
  } else {
    tet_pos = t->len + t->pos - sizeof(tet);
  }
  copy_from_pos(t, tet_pos, sizeof(tet), &tet);

  /* check that event fits in the buffer */
  data_len = tet.length;
  if (len < data_len) {
    fprintf(stderr, "trace_prev: buffer too small\n");
    return -1;
  }

  /* copy event data into buffer */
  if (tet_pos >= data_len) {
    data_pos = tet_pos - data_len;
  } else {
    data_pos = t->len + tet_pos - data_len;
  }
  copy_from_pos(t, data_pos, data_len, buf);

  /* copy head */
  if (data_pos >= sizeof(teh)) {
    teh_pos = data_pos - sizeof(teh);
  } else {
    teh_pos = t->len + data_pos - sizeof(teh);
  }
  copy_from_pos(t, teh_pos, sizeof(teh), &teh);

  /* validate head */
  if (teh.length != data_len) {
    fprintf(stderr, "trace_prev: head length does not match\n");
    return -1;
  }
  if (teh.type == 0) {
    fprintf(stderr, "type is zero \n");
    return -1;
  }
  *ts = teh.ts;
  *type = teh.type;
  *seq = teh.seq;

  /* update position */
  t->pos = teh_pos;

  return data_len;

}

static inline void copy_from_pos(struct trace *t, size_t pos, size_t len,
    void *dst)
{
  size_t first;

  if (pos >= t->len) {
    pos -= t->len;
  }
  assert(pos < t->len);

  if (pos + len <= t->len) {
    memcpy(dst, (uint8_t *) t->base + pos, len);
  } else {
    first = t->len - pos;
    memcpy(dst, (uint8_t *) t->base + pos, first);
    memcpy((uint8_t *) dst + first, t->base, len - first);
  }

}

static void event_dump(void *buf, size_t len, uint16_t type)
{
  struct flextcp_pl_trev_atx *atx = buf;
  struct flextcp_pl_trev_arx *arx = buf;
  struct flextcp_pl_trev_rxfs *rxfs = buf;
  struct flextcp_pl_trev_txack *txack = buf;
  struct flextcp_pl_trev_txseg *txseg = buf;
  struct flextcp_pl_trev_afloqman *floq = buf;
  struct flextcp_pl_trev_rexmit *rex = buf;

  switch (type) {
    case FLEXNIC_TRACE_EV_RXPKT:
      printf("FLEXNIC_EV_RXPKT  ");
      packet_dump(buf, len);
      break;

    case FLEXNIC_TRACE_EV_TXPKT:
      printf("FLEXNIC_EV_TXPKT  ");
      packet_dump(buf, len);
      break;

    case FLEXNIC_TRACE_EV_DMARD:
      printf("FLEXNIC_EV_DMARD  ");
      dma_dump(buf, len);
      break;

    case FLEXNIC_TRACE_EV_DMAWR:
      printf("FLEXNIC_EV_DMAWR  ");
      dma_dump(buf, len);
      break;

    case FLEXNIC_TRACE_EV_QMSET:
      printf("FLEXNIC_EV_QMSET  ");
      qmset_dump(buf, len);
      break;

    case FLEXNIC_TRACE_EV_QMEVT:
      printf("FLEXNIC_EV_QMEVT  ");
      qmevt_dump(buf, len);
      break;

    case FLEXNIC_PL_TREV_ATX:
      printf("FLEXTCP_EV_ATX ");
      if (sizeof(*atx) == len) {
        printf("rx_bump=%u tx_bump=%u bump_seq_ent=%u bump_seq_flow=%u "
            "flags=%x "
            "local_ip=%x remote_ip=%x local_port=%u remote_port=%u "
            "flow=%u db=%u tx_next_pos=%u tx_next_seq=%u tx_avail_prev=%u "
            "rx_next_pos=%u rx_avail=%u tx_len=%u rx_len=%u "
            "rx_remote_avail=%u tx_sent=%u",
            atx->rx_bump, atx->tx_bump, atx->bump_seq_ent, atx->bump_seq_flow,
            atx->flags,
            atx->local_ip, atx->remote_ip, atx->local_port, atx->remote_port,
            atx->flow_id, atx->db_id, atx->tx_next_pos, atx->tx_next_seq,
            atx->tx_avail_prev, atx->rx_next_pos, atx->rx_avail, atx->tx_len,
            atx->rx_len, atx->rx_remote_avail, atx->tx_sent);
      } else {
        printf("unexpected event length");
      }
      break;

    case FLEXNIC_PL_TREV_ARX:
      printf("FLEXTCP_EV_ARX ");
      if (sizeof(*arx) == len) {
        printf("opaque=%lx rx_bump=%u rx_pos=%u tx_bump=%u flags=%x flow=%u "
            "db=%u local_ip=%x remote_ip=%x local_port=%u remote_port=%u",
            arx->opaque, arx->rx_bump, arx->rx_pos, arx->tx_bump, arx->flags,
            arx->flow_id, arx->db_id, arx->local_ip, arx->remote_ip,
            arx->local_port, arx->remote_port);
      } else {
        printf("unexpected event length");
      }
      break;

    case FLEXNIC_PL_TREV_RXFS:
      printf("FLEXTCP_EV_RXFS ");
      if (sizeof(*rxfs) == len) {
        printf("local_ip=%x remote_ip=%x local_port=%u remote_port=%u "
            "flow_id=%u flow={seq=%u ack=%u flags=%x len=%u} fs={rx_nextpos=%u "
            " rx_nextseq=%u rx_avail=%u  tx_nextpos=%u tx_nextseq=%u "
            "tx_sent=%u tx_avail=%u payload={",
            rxfs->local_ip, rxfs->remote_ip, rxfs->local_port,
            rxfs->remote_port, rxfs->flow_id, rxfs->flow_seq, rxfs->flow_ack,
            rxfs->flow_flags, rxfs->flow_len, rxfs->fs_rx_nextpos,
            rxfs->fs_rx_nextseq,
            rxfs->fs_rx_avail, rxfs->fs_tx_nextpos, rxfs->fs_tx_nextseq,
            rxfs->fs_tx_sent, rxfs->fs_tx_avail);

	/* uint8_t *payload = buf; */
	/* printf(" payload={"); */
	/* for(int i = 0; i < len; i++) { */
	/*   if(i % 16 == 0) { */
	/*     printf("\n%08X  ", i); */
	/*   } */
	/*   if(i % 4 == 0) { */
	/*     printf(" "); */
	/*   } */
	/*   printf("%02X ", payload[i]); */
	/* } */
	/* printf("}"); */
      } else {
        printf("unexpected event length");
      }
      break;

    case FLEXNIC_PL_TREV_AFLOQMAN:
      printf("FLEXTCP_EV_AFLOQMAN ");
      if (sizeof(*floq) == len) {
        printf("tx_base=%"PRIx64" tx_avail=%u tx_next_pos=%u tx_len=%u "
            "rx_remote_avail=%u tx_sent=%u tx_objrem=%u flow=%u",
            floq->tx_base, floq->tx_avail, floq->tx_next_pos, floq->tx_len,
            floq->rx_remote_avail, floq->tx_sent, floq->tx_avail,
            floq->flow_id);
     } else {
        printf("unexpected event length");
      }

      break;

    case FLEXNIC_PL_TREV_REXMIT:
      printf("FLEXTCP_EV_REXMIT ");
      if (sizeof(*rex) == len) {
        printf("flow_id=%u tx_avail=%u tx_sent=%u tx_next_pos=%u "
            "tx_next_seq=%u rx_remote_avail=%u",
            rex->flow_id, rex->tx_avail, rex->tx_sent, rex->tx_next_pos,
            rex->tx_next_seq, floq->rx_remote_avail);
     } else {
        printf("unexpected event length");
      }

      break;

    case FLEXNIC_PL_TREV_TXACK:
      printf("FLEXTCP_EV_TXACK ");
      if (sizeof(*txack) == len) {
        printf("local_ip=%x remote_ip=%x local_port=%u remote_port=%u "
            "flow_seq=%u flow_ack=%u flow_flags=%x",
            txack->local_ip, txack->remote_ip, txack->local_port,
            txack->remote_port, txack->flow_seq, txack->flow_ack,
            txack->flow_flags);
      } else {
        printf("unexpected event length");
      }
      break;

    case FLEXNIC_PL_TREV_TXSEG:
      printf("FLEXTCP_EV_TXSEG ");
      if (sizeof(*txseg) == len) {
        printf("local_ip=%x remote_ip=%x local_port=%u remote_port=%u "
            "flow_seq=%u flow_ack=%u flow_flags=%x flow_len=%u",
            txseg->local_ip, txseg->remote_ip, txseg->local_port,
            txseg->remote_port, txseg->flow_seq, txseg->flow_ack,
            txseg->flow_flags, txseg->flow_len);
      } else {
        printf("unexpected event length");
      }
      break;

    default:
      printf("unknown event=%u", type);
      break;
  }
  printf("\n");
}

static void packet_dump(void *buf, size_t len)
{
  struct eth_hdr *eth = buf;
  struct arp_hdr *arp = (struct arp_hdr *) (eth + 1);
  struct ip_hdr *ip = (struct ip_hdr *) (eth + 1);
  struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
  uint8_t *payload = (uint8_t *)(tcp + 1);

  uint64_t sm, dm;
  uint16_t et, iplen, tcplen;

  if (len < sizeof(*eth)) {
    printf("ill formated (short) Ethernet packet");
    return;
  }

  sm = dm = 0;
  memcpy(&sm, &eth->src, ETH_ADDR_LEN);
  memcpy(&dm, &eth->dest, ETH_ADDR_LEN);
  et = f_beui16(eth->type);
  printf("eth={src=%"PRIx64" dst=%"PRIx64" type=%x}", sm, dm, et);

  if (et == ETH_TYPE_IP) {
    if (len < sizeof(*eth) + sizeof(*ip)) {
      printf(" ill formated (short) IPv4 packet");
      return;
    }

    printf(" ip={src=%x dst=%x proto=%x}", f_beui32(ip->src),
        f_beui32(ip->dest), ip->proto);
    iplen = f_beui16(ip->len) - sizeof(*ip);

    if (ip->proto == IP_PROTO_TCP) {
      if (len < sizeof(*eth) + sizeof(*ip) + sizeof(*tcp)) {
        printf(" ill formated (short) TCP packet");
        return;
      }
      tcplen = iplen - sizeof(*tcp) - (TCPH_HDRLEN(tcp) - 5) * 4;
      printf(" tcp={src=%u dst=%u flags=%x seq=%u ack=%u wnd=%u len=%u}",
          f_beui16(tcp->src), f_beui16(tcp->dest), TCPH_FLAGS(tcp),
          f_beui32(tcp->seqno), f_beui32(tcp->ackno), f_beui16(tcp->wnd),
          tcplen);

      printf(" payload={");
      for(int i = 0; i < tcplen; i++) {
	if(i % 16 == 0) {
	  printf("\n%08X  ", i);
	}
	if(i % 4 == 0) {
	  printf(" ");
	}
	printf("%02X ", payload[i]);
      }
      printf("}");
    }
  } else if (et == ETH_TYPE_ARP) {
    memcpy(&sm, &arp->sha, ETH_ADDR_LEN);
    memcpy(&dm, &arp->tha, ETH_ADDR_LEN);
    printf(" arp={oper=%u spa=%x tpa=%x sha=%"PRIx64" tha=%"PRIx64"}",
        f_beui16(arp->oper), f_beui32(arp->spa), f_beui32(arp->tpa), sm, dm);
  }
}

static void dma_dump(void *buf, size_t len)
{
  struct flexnic_trace_entry_dma *hdr = buf;

  if (sizeof(*hdr) > len) {
    printf("ill formated (short) dma header");
    return;
  }

  printf(" dma={addr=%"PRIx64" len=%"PRIx64"}", hdr->addr, hdr->len);

  uint8_t *payload = hdr->data;
  printf(" payload={");
  for(int i = 0; i < hdr->len; i++) {
    if(i % 16 == 0) {
      printf("\n%08X  ", i);
    }
    if(i % 4 == 0) {
      printf(" ");
    }
    printf("%02X ", payload[i]);
  }
  printf("}");
}

static void qmset_dump(void *buf, size_t len)
{
  struct flexnic_trace_entry_qman_set *hdr = buf;

  if (sizeof(*hdr) > len) {
    printf("ill formated (short) qmset header");
    return;
  }

  printf(" cfg={id=%u rate=%u avail=%u max_chunk=%u opaque=%x flags=%x}",
      hdr->id, hdr->rate, hdr->avail, hdr->max_chunk, hdr->opaque, hdr->flags);
}

static void qmevt_dump(void *buf, size_t len)
{
  struct flexnic_trace_entry_qman_event *hdr = buf;

  if (sizeof(*hdr) > len) {
    printf("ill formated (short) qmevt header");
    return;
  }

  printf(" cfg={id=%u bytes=%u opaque=%x}", hdr->id, hdr->bytes, hdr->opaque);
}
