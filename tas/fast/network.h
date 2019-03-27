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

#ifndef NETWORK_H_
#define NETWORK_H_

#include <rte_config.h>
#include <rte_memcpy.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ip.h>

#include <fastpath.h>

struct network_buf_handle;

extern uint8_t net_port_id;
extern uint16_t rss_reta_size;

int network_thread_init(struct dataplane_context *ctx);
int network_rx_interrupt_ctl(struct network_thread *t, int turnon);

int network_scale_up(uint16_t old, uint16_t new);
int network_scale_down(uint16_t old, uint16_t new);


static inline void network_buf_reset(struct network_buf_handle *bh)
{
  struct rte_mbuf *mb = (struct rte_mbuf *) bh;
  mb->ol_flags = 0;
}

static inline uint16_t network_buf_off(struct network_buf_handle *bh)
{
  return ((struct rte_mbuf *) bh)->data_off;
}

static inline uint16_t network_buf_len(struct network_buf_handle *bh)
{
  return ((struct rte_mbuf *) bh)->data_len;
}

static inline void *network_buf_buf(struct network_buf_handle *bh)
{
  return ((struct rte_mbuf *) bh)->buf_addr;
}

static inline void *network_buf_bufoff(struct network_buf_handle *bh)
{
  struct rte_mbuf *mb = (struct rte_mbuf *) bh;
  return mb->buf_addr + mb->data_off;
}

static inline void network_buf_setoff(struct network_buf_handle *bh,
    uint16_t off)
{
  ((struct rte_mbuf *) bh)->data_off = off;
}

static inline void network_buf_setlen(struct network_buf_handle *bh,
    uint16_t len)
{
  struct rte_mbuf *mb = (struct rte_mbuf *) bh;
  mb->pkt_len = mb->data_len = len;
}


static inline int network_poll(struct network_thread *t, unsigned num,
    struct network_buf_handle **bhs)
{
  struct rte_mbuf **mbs = (struct rte_mbuf **) bhs;

  num = rte_eth_rx_burst(net_port_id, t->queue_id, mbs, num);
  if (num == 0) {
    return 0;
  }

#ifdef FLEXNIC_TRACE_TX
  unsigned i;
  for (i = 0; i < num; i++) {
    trace_event(FLEXNIC_TRACE_EV_RXPKT, network_buf_len(bhs[i]),
        network_buf_bufoff(bhs[i]));
  }
#endif

  return num;
}

static inline int network_send(struct network_thread *t, unsigned num,
    struct network_buf_handle **bhs)
{
  struct rte_mbuf **mbs = (struct rte_mbuf **) bhs;

#ifdef FLEXNIC_TRACE_TX
  unsigned i;
  for (i = 0; i < num; i++) {
    trace_event(FLEXNIC_TRACE_EV_TXPKT, network_buf_len(bhs[i]),
        network_buf_bufoff(bhs[i]));
  }
#endif

  return rte_eth_tx_burst(net_port_id, t->queue_id, mbs, num);
}


static inline int network_buf_alloc(struct network_thread *t, unsigned num,
    struct network_buf_handle **bhs)
{
  struct rte_mbuf **mbs = (struct rte_mbuf **) bhs;
  unsigned i;

  /* try bulk alloc first. if it fails try individual mbufs */
  if (rte_pktmbuf_alloc_bulk(t->pool, mbs, num) == 0) {
    return num;
  }

  for (i = 0; i < num; i++) {
    if ((mbs[i] = rte_pktmbuf_alloc(t->pool)) == NULL) {
      break;
    }
  }

  return i;
}

static inline void network_free(unsigned num, struct network_buf_handle **bufs)
{
  unsigned i;
  for (i = 0; i < num; i++) {
    rte_pktmbuf_free_seg((struct rte_mbuf *) bufs[i]);
  }
}

/** calculate ip pseudo header xsum */
static inline uint16_t network_ip_phdr_xsum(beui32_t ip_src, beui32_t ip_dst,
    uint8_t proto, uint16_t l3_paylen)
{
  uint32_t sum = 0;

  sum += ip_src.x & 0xffff;
  sum += (ip_src.x >> 16) & 0xffff;
  sum += ip_dst.x & 0xffff;
  sum += (ip_dst .x >> 16) & 0xffff;
  sum += ((uint16_t) proto) << 8;
  sum += t_beui16(l3_paylen).x;

  sum = ((sum & 0xffff0000) >> 16) + (sum & 0xffff);
  sum = ((sum & 0xffff0000) >> 16) + (sum & 0xffff);

  return (uint16_t) sum;
}

static inline uint16_t network_buf_tcpxsums(struct network_buf_handle *bh, uint8_t l2l,
    uint8_t l3l, void *ip_hdr, beui32_t ip_s, beui32_t ip_d, uint8_t ip_proto,
    uint16_t l3_paylen)
{
  struct rte_mbuf * restrict mb = (struct rte_mbuf *) bh;
  mb->tx_offload = l2l | ((uint32_t) l3l << 7);
  /*mb->l2_len = l2l;
  mb->l3_len = l3l;
  mb->l4_len = 0;*/
  mb->ol_flags = PKT_TX_IPV4 | PKT_TX_IP_CKSUM | PKT_TX_TCP_CKSUM;

  return network_ip_phdr_xsum(ip_s, ip_d, ip_proto, l3_paylen);
}

static inline int network_buf_flowgroup(struct network_buf_handle *bh,
    uint16_t *fg)
{
  struct rte_mbuf *mb = (struct rte_mbuf *) bh;
  if (!(mb->ol_flags & PKT_RX_RSS_HASH)) {
    *fg = 0;
    return 0;
  }

  *fg = mb->hash.rss & (rss_reta_size - 1);
  return 0;
}

#endif /* ndef NETWORK_H_ */
