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
#include <rte_config.h>
#include <rte_ip.h>
#include <rte_hash_crc.h>

#include <tas_memif.h>
#include <utils_sync.h>

#include "internal.h"
#include "fastemu.h"
#include "tcp_common.h"

#define TCP_MSS 1448
#define TCP_MAX_RTT 100000

#define HWXSUM_EN 1

//#define SKIP_ACK 1

struct flow_key {
  ip_addr_t local_ip;
  ip_addr_t remote_ip;
  beui16_t local_port;
  beui16_t remote_port;
} __attribute__((packed));

#if 1
#define fs_lock(fs) util_spin_lock(&fs->lock)
#define fs_unlock(fs) util_spin_unlock(&fs->lock)
#else
#define fs_lock(fs) do {} while (0)
#define fs_unlock(fs) do {} while (0)
#endif


static void flow_tx_read(struct flextcp_pl_flowst *fs, uint32_t pos,
    uint16_t len, void *dst);
static void flow_rx_write(struct flextcp_pl_flowst *fs, uint32_t pos,
    uint16_t len, const void *src);
#ifdef FLEXNIC_PL_OOO_RECV
static void flow_rx_seq_write(struct flextcp_pl_flowst *fs, uint32_t seq,
    uint16_t len, const void *src);
#endif
static void flow_tx_segment(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, struct flextcp_pl_flowst *fs,
    uint32_t seq, uint32_t ack, uint32_t rxwnd, uint16_t payload,
    uint32_t payload_pos, uint32_t ts_echo, uint32_t ts_my, uint8_t fin);
static void flow_tx_ack(struct dataplane_context *ctx, uint32_t seq,
    uint32_t ack, uint32_t rxwnd, uint32_t echo_ts, uint32_t my_ts,
    struct network_buf_handle *nbh, struct tcp_timestamp_opt *ts_opt);
static void flow_reset_retransmit(struct flextcp_pl_flowst *fs);

static inline void tcp_checksums(struct network_buf_handle *nbh,
    struct pkt_tcp *p, beui32_t ip_s, beui32_t ip_d, uint16_t l3_paylen);

void fast_flows_qman_pf(struct dataplane_context *ctx, uint32_t *queues,
    uint16_t n)
{
  uint16_t i;

  for (i = 0; i < n; i++) {
    rte_prefetch0(&fp_state->flowst[queues[i]]);
  }
}

void fast_flows_qman_pfbufs(struct dataplane_context *ctx, uint32_t *queues,
    uint16_t n)
{
  struct flextcp_pl_flowst *fs;
  uint16_t i;
  void *p;

  for (i = 0; i < n; i++) {
    fs = &fp_state->flowst[queues[i]];
    p = dma_pointer(fs->tx_base + fs->tx_next_pos, 1);
    rte_prefetch0(p);
    rte_prefetch0(p + 64);
  }
}


int fast_flows_qman(struct dataplane_context *ctx, uint32_t queue,
    struct network_buf_handle *nbh, uint32_t ts)
{
  uint32_t flow_id = queue;
  struct flextcp_pl_flowst *fs = &fp_state->flowst[flow_id];
  uint32_t avail, len, tx_pos, tx_seq, ack, rx_wnd;
  uint16_t new_core;
  uint8_t fin;
  int ret = 0;

  fs_lock(fs);

  /* if connection has been moved, add to forwarding queue and stop */
  new_core = fp_state->flow_group_steering[fs->flow_group];
  if (new_core != ctx->id) {
    /*fprintf(stderr, "fast_flows_qman: arrived on wrong core, forwarding "
        "%u -> %u (fs=%p, fg=%u)\n", ctx->id, new_core, fs, fs->flow_group);*/

    /* enqueue flo state on forwarding queue */
    if (rte_ring_enqueue(ctxs[new_core]->qman_fwd_ring, fs) != 0) {
      fprintf(stderr, "fast_flows_qman: rte_ring_enqueue failed\n");
      abort();
    }

    /* clear queue manager queue */
    if (qman_set(&ctx->qman, flow_id, 0, 0, 0,
          QMAN_SET_RATE | QMAN_SET_MAXCHUNK | QMAN_SET_AVAIL) != 0)
    {
      fprintf(stderr, "flast_flows_qman: qman_set clear failed, UNEXPECTED\n");
      abort();
    }

    util_flexnic_kick(&fp_state->kctx[new_core], ts);

    ret = -1;
    goto unlock;
  }

  /* calculate how much is available to be sent */
  avail = tcp_txavail(fs, NULL);

#if PL_DEBUG_ATX
  fprintf(stderr, "ATX try_sendseg local=%08x:%05u remote=%08x:%05u "
      "tx_avail=%x tx_next_pos=%x avail=%u\n",
      f_beui32(fs->local_ip), f_beui16(fs->local_port),
      f_beui32(fs->remote_ip), f_beui16(fs->remote_port),
      fs->tx_avail, fs->tx_next_pos, avail);
#endif
#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_afloqman te_afloqman = {
      .flow_id = flow_id,
      .tx_base = fs->tx_base,
      .tx_avail = fs->tx_avail,
      .tx_next_pos = fs->tx_next_pos,
      .tx_len = fs->tx_len,
      .rx_remote_avail = fs->rx_remote_avail,
      .tx_sent = fs->tx_sent,
    };
  trace_event(FLEXNIC_PL_TREV_AFLOQMAN, sizeof(te_afloqman), &te_afloqman);
#endif

  /* if there is no data available, stop */
  if (avail == 0) {
    ret = -1;
    goto unlock;
  }
  len = MIN(avail, TCP_MSS);

  /* state snapshot for creating segment */
  tx_seq = fs->tx_next_seq;
  tx_pos = fs->tx_next_pos;
  rx_wnd = fs->rx_avail;
  ack = fs->rx_next_seq;

  /* update tx flow state */
  fs->tx_next_seq += len;
  fs->tx_next_pos += len;
  if (fs->tx_next_pos >= fs->tx_len) {
    fs->tx_next_pos -= fs->tx_len;
  }
  fs->tx_sent += len;
  fs->tx_avail -= len;

  fin = (fs->rx_base_sp & FLEXNIC_PL_FLOWST_TXFIN) == FLEXNIC_PL_FLOWST_TXFIN &&
    !fs->tx_avail;

  /* make sure we don't send out dummy byte for FIN */
  if (fin) {
    assert(len > 0);
    len--;
  }

  /* send out segment */
  flow_tx_segment(ctx, nbh, fs, tx_seq, ack, rx_wnd, len, tx_pos,
      fs->tx_next_ts, ts, fin);
unlock:
  fs_unlock(fs);
  return ret;
}

int fast_flows_qman_fwd(struct dataplane_context *ctx,
    struct flextcp_pl_flowst *fs)
{
  unsigned avail;
  uint16_t flow_id = fs - fp_state->flowst;

  /*fprintf(stderr, "fast_flows_qman_fwd: fs=%p\n", fs);*/

  fs_lock(fs);

  avail = tcp_txavail(fs, NULL);

  /* re-arm queue manager */
  if (qman_set(&ctx->qman, flow_id, fs->tx_rate, avail, TCP_MSS,
        QMAN_SET_RATE | QMAN_SET_MAXCHUNK | QMAN_SET_AVAIL) != 0)
  {
    fprintf(stderr, "fast_flows_qman_fwd: qman_set failed, UNEXPECTED\n");
    abort();
  }

  fs_unlock(fs);
  return 0;
}

void fast_flows_packet_parse(struct dataplane_context *ctx,
    struct network_buf_handle **nbhs, void **fss, struct tcp_opts *tos,
    uint16_t n)
{
  struct pkt_tcp *p;
  uint16_t i, len;

  for (i = 0; i < n; i++) {
    if (fss[i] == NULL)
      continue;

    p = network_buf_bufoff(nbhs[i]);
    len = network_buf_len(nbhs[i]);

    int cond =
        (len < sizeof(*p)) |
        (f_beui16(p->eth.type) != ETH_TYPE_IP) |
        (p->ip.proto != IP_PROTO_TCP) |
        (IPH_V(&p->ip) != 4) |
        (IPH_HL(&p->ip) != 5) |
        (TCPH_HDRLEN(&p->tcp) < 5) |
        (len < f_beui16(p->ip.len) + sizeof(p->eth)) |
        (tcp_parse_options(p, len, &tos[i]) != 0) |
        (tos[i].ts == NULL);

    if (cond)
      fss[i] = NULL;
  }
}

void fast_flows_packet_pfbufs(struct dataplane_context *ctx,
    void **fss, uint16_t n)
{
  uint16_t i;
  uint64_t rx_base;
  void *p;
  struct flextcp_pl_flowst *fs;

  for (i = 0; i < n; i++) {
    if (fss[i] == NULL)
      continue;

    fs = fss[i];
    rx_base = fs->rx_base_sp & FLEXNIC_PL_FLOWST_RX_MASK;
    p = dma_pointer(rx_base + fs->rx_next_pos, 1);
    rte_prefetch0(p);
  }
}

/* Received packet */
int fast_flows_packet(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, void *fsp, struct tcp_opts *opts,
    uint32_t ts)
{
  struct pkt_tcp *p = network_buf_bufoff(nbh);
  struct flextcp_pl_flowst *fs = fsp;
  uint32_t payload_bytes, payload_off, seq, ack, old_avail, new_avail,
           orig_payload;
  uint8_t *payload;
  uint32_t rx_bump = 0, tx_bump = 0, rx_pos, rtt;
  int no_permanent_sp = 0;
  uint16_t tcp_extra_hlen, trim_start, trim_end;
  uint16_t flow_id = fs - fp_state->flowst;
  int trigger_ack = 0, fin_bump = 0;

  tcp_extra_hlen = (TCPH_HDRLEN(&p->tcp) - 5) * 4;
  payload_off = sizeof(*p) + tcp_extra_hlen;
  payload_bytes =
      f_beui16(p->ip.len) - (sizeof(p->ip) + sizeof(p->tcp) + tcp_extra_hlen);
  orig_payload = payload_bytes;

#if PL_DEBUG_ARX
  fprintf(stderr, "FLOW local=%08x:%05u remote=%08x:%05u  RX: seq=%u ack=%u "
      "flags=%x payload=%u\n",
      f_beui32(p->ip.dest), f_beui16(p->tcp.dest),
      f_beui32(p->ip.src), f_beui16(p->tcp.src), f_beui32(p->tcp.seqno),
      f_beui32(p->tcp.ackno), TCPH_FLAGS(&p->tcp), payload_bytes);
#endif

  fs_lock(fs);

#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_rxfs te_rxfs = {
      .local_ip = f_beui32(p->ip.dest),
      .remote_ip = f_beui32(p->ip.src),
      .local_port = f_beui16(p->tcp.dest),
      .remote_port = f_beui16(p->tcp.src),

      .flow_seq = f_beui32(p->tcp.seqno),
      .flow_ack = f_beui32(p->tcp.ackno),
      .flow_flags = TCPH_FLAGS(&p->tcp),
      .flow_len = payload_bytes,

      .fs_rx_nextpos = fs->rx_next_pos,
      .fs_rx_nextseq = fs->rx_next_seq,
      .fs_rx_avail = fs->rx_avail,
      .fs_tx_nextpos = fs->tx_next_pos,
      .fs_tx_nextseq = fs->tx_next_seq,
      .fs_tx_sent = fs->tx_sent,
    };
  trace_event(FLEXNIC_PL_TREV_RXFS, sizeof(te_rxfs), &te_rxfs);
#endif

#if PL_DEBUG_ARX
  fprintf(stderr, "FLOW local=%08x:%05u remote=%08x:%05u  ST: op=%"PRIx64
      " rx_pos=%x rx_next_seq=%u rx_avail=%x  tx_pos=%x tx_next_seq=%u"
      " tx_sent=%u sp=%u\n",
      f_beui32(p->ip.dest), f_beui16(p->tcp.dest),
      f_beui32(p->ip.src), f_beui16(p->tcp.src), fs->opaque, fs->rx_next_pos,
      fs->rx_next_seq, fs->rx_avail, fs->tx_next_pos, fs->tx_next_seq,
      fs->tx_sent, fs->slowpath);
#endif

  /* state indicates slow path */
  if (UNLIKELY((fs->rx_base_sp & FLEXNIC_PL_FLOWST_SLOWPATH) != 0)) {
    fprintf(stderr, "dma_krx_pkt_fastpath: slowpath because of state\n");
    goto slowpath;
  }

  /* if we get weird flags -> kernel */
  if (UNLIKELY((TCPH_FLAGS(&p->tcp) & ~(TCP_ACK | TCP_PSH | TCP_ECE | TCP_CWR |
            TCP_FIN)) != 0))
  {
    if ((TCPH_FLAGS(&p->tcp) & TCP_SYN) != 0) {
      /* for SYN/SYN-ACK we'll let the kernel handle them out of band */
      no_permanent_sp = 1;
    } else {
      fprintf(stderr, "dma_krx_pkt_fastpath: slow path because of flags (%x)\n",
          TCPH_FLAGS(&p->tcp));
    }
    goto slowpath;
  }

  /* calculate how much data is available to be sent before processing this
   * packet, to detect whether more data can be sent afterwards */
  old_avail = tcp_txavail(fs, NULL);

  seq = f_beui32(p->tcp.seqno);
  ack = f_beui32(p->tcp.ackno);
  rx_pos = fs->rx_next_pos;

  /* trigger an ACK if there is payload (even if we discard it) */
#ifndef SKIP_ACK
  if (payload_bytes > 0)
    trigger_ack = 1;
#endif

  /* Stats for CC */
  if ((TCPH_FLAGS(&p->tcp) & TCP_ACK) == TCP_ACK) {
    fs->cnt_rx_acks++;
  }

  /* if there is a valid ack, process it */
  if (LIKELY((TCPH_FLAGS(&p->tcp) & TCP_ACK) == TCP_ACK &&
      tcp_valid_rxack(fs, ack, &tx_bump) == 0))
  {
    fs->cnt_rx_ack_bytes += tx_bump;
    if ((TCPH_FLAGS(&p->tcp) & TCP_ECE) == TCP_ECE) {
      fs->cnt_rx_ecn_bytes += tx_bump;
    }

    if (LIKELY(tx_bump <= fs->tx_sent)) {
      fs->tx_sent -= tx_bump;
    } else {
#ifdef ALLOW_FUTURE_ACKS
      fs->tx_next_seq += tx_bump - fs->tx_sent;
      fs->tx_next_pos += tx_bump - fs->tx_sent;
      if (fs->tx_next_pos >= fs->tx_len)
        fs->tx_next_pos -= fs->tx_len;
      fs->tx_sent = 0;
#else
      /* this should not happen */
      fprintf(stderr, "dma_krx_pkt_fastpath: acked more bytes than sent\n");
      abort();
#endif
    }

    /* duplicate ack */
    if (UNLIKELY(tx_bump != 0)) {
      fs->rx_dupack_cnt = 0;
    } else if (UNLIKELY(orig_payload == 0 && ++fs->rx_dupack_cnt >= 3)) {
      /* reset to last acknowledged position */
      flow_reset_retransmit(fs);
      goto unlock;
    }
  }

#ifdef FLEXNIC_PL_OOO_RECV
  /* check if we should drop this segment */
  if (UNLIKELY(tcp_trim_rxbuf(fs, seq, payload_bytes, &trim_start, &trim_end) != 0)) {
    /* packet is completely outside of unused receive buffer */
    goto unlock;
  }

  /* trim payload to what we can actually use */
  payload_bytes -= trim_start + trim_end;
  payload_off += trim_start;
  payload = (uint8_t *) p + payload_off;
  seq += trim_start;

  /* handle out of order segment */
  if (UNLIKELY(seq != fs->rx_next_seq)) {
    trigger_ack = 1;

    /* if there is no payload abort immediately */
    if (payload_bytes == 0) {
      goto unlock;
    }

    /* otherwise check if we can add it to the out of order interval */
    if (fs->rx_ooo_len == 0) {
      fs->rx_ooo_start = seq;
      fs->rx_ooo_len = payload_bytes;
      flow_rx_seq_write(fs, seq, payload_bytes, payload);
      /*fprintf(stderr, "created OOO interval (%p start=%u len=%u)\n",
          fs, fs->rx_ooo_start, fs->rx_ooo_len);*/
    } else if (seq + payload_bytes == fs->rx_ooo_start) {
      /* TODO: those two overlap checks should be more sophisticated */
      fs->rx_ooo_start = seq;
      fs->rx_ooo_len += payload_bytes;
      flow_rx_seq_write(fs, seq, payload_bytes, payload);
      /*fprintf(stderr, "extended OOO interval (%p start=%u len=%u)\n",
          fs, fs->rx_ooo_start, fs->rx_ooo_len);*/
    } else if (fs->rx_ooo_start + fs->rx_ooo_len == seq) {
      /* TODO: those two overlap checks should be more sophisticated */
      fs->rx_ooo_len += payload_bytes;
      flow_rx_seq_write(fs, seq, payload_bytes, payload);
      /*fprintf(stderr, "extended OOO interval (%p start=%u len=%u)\n",
          fs, fs->rx_ooo_start, fs->rx_ooo_len);*/
    } else {
      /*fprintf(stderr, "Sad, no luck with OOO interval (%p ooo.start=%u "
          "ooo.len=%u seq=%u bytes=%u)\n", fs, fs->rx_ooo_start,
          fs->rx_ooo_len, seq, payload_bytes);*/
    }
    goto unlock;
  }

#else
  /* check if we should drop this segment */
  if (tcp_valid_rxseq(fs, seq, payload_bytes, &trim_start, &trim_end) != 0) {
#if 0
    fprintf(stderr, "dma_krx_pkt_fastpath: packet with bad seq "
        "(got %u, expect %u, avail %u, payload %u)\n", seq, fs->rx_next_seq,
        fs->rx_avail, payload_bytes);
#endif
    goto unlock;
  }

  /* trim payload to what we can actually use */
  payload_bytes -= trim_start + trim_end;
  payload_off += trim_start;
  payload = (uint8_t *) p + payload_off;
#endif

  /* update rtt estimate */
  fs->tx_next_ts = f_beui32(opts->ts->ts_val);
  if (LIKELY((TCPH_FLAGS(&p->tcp) & TCP_ACK) == TCP_ACK &&
      f_beui32(opts->ts->ts_ecr) != 0))
  {
    rtt = ts - f_beui32(opts->ts->ts_ecr);
    if (rtt < TCP_MAX_RTT) {
      if (LIKELY(fs->rtt_est != 0)) {
        fs->rtt_est = (fs->rtt_est * 7 + rtt) / 8;
      } else {
        fs->rtt_est = rtt;
      }
    }
  }

  fs->rx_remote_avail = f_beui16(p->tcp.wnd);

  /* make sure we don't receive anymore payload after FIN */
  if ((fs->rx_base_sp & FLEXNIC_PL_FLOWST_RXFIN) == FLEXNIC_PL_FLOWST_RXFIN &&
      payload_bytes > 0)
  {
    fprintf(stderr, "fast_flows_packet: data after FIN dropped\n");
    goto unlock;
  }

  /* if there is payload, dma it to the receive buffer */
  if (payload_bytes > 0) {
    flow_rx_write(fs, fs->rx_next_pos, payload_bytes, payload);

    rx_bump = payload_bytes;
    fs->rx_avail -= payload_bytes;
    fs->rx_next_pos += payload_bytes;
    if (fs->rx_next_pos >= fs->rx_len) {
      fs->rx_next_pos -= fs->rx_len;
    }
    assert(fs->rx_next_pos < fs->rx_len);
    fs->rx_next_seq += payload_bytes;
#ifndef SKIP_ACK
    trigger_ack = 1;
#endif

#ifdef FLEXNIC_PL_OOO_RECV
    /* if we have out of order segments, check whether buffer is continuous
     * or superfluous */
    if (UNLIKELY(fs->rx_ooo_len != 0)) {
      if (tcp_trim_rxbuf(fs, fs->rx_ooo_start, fs->rx_ooo_len, &trim_start,
            &trim_end) != 0) {
          /*fprintf(stderr, "dropping ooo (%p ooo.start=%u ooo.len=%u seq=%u "
              "len=%u next_seq=%u)\n", fs, fs->rx_ooo_start, fs->rx_ooo_len, seq,
              payload_bytes, fs->rx_next_seq);*/
        /* completely superfluous: drop out of order interval */
        fs->rx_ooo_len = 0;
      } else {
        /* adjust based on overlap */
        fs->rx_ooo_start += trim_start;
        fs->rx_ooo_len -= trim_start + trim_end;
        /*fprintf(stderr, "adjusting ooo (%p ooo.start=%u ooo.len=%u seq=%u "
            "len=%u next_seq=%u)\n", fs, fs->rx_ooo_start, fs->rx_ooo_len, seq,
            payload_bytes, fs->rx_next_seq);*/
        if (fs->rx_ooo_len > 0 && fs->rx_ooo_start == fs->rx_next_seq) {
          /* yay, we caught up, make continuous and drop OOO interval */
          /*fprintf(stderr, "caught up with ooo buffer (%p start=%u len=%u)\n",
              fs, fs->rx_ooo_start, fs->rx_ooo_len);*/

          rx_bump += fs->rx_ooo_len;
          fs->rx_avail -= fs->rx_ooo_len;
          fs->rx_next_pos += fs->rx_ooo_len;
          if (fs->rx_next_pos >= fs->rx_len) {
            fs->rx_next_pos -= fs->rx_len;
          }
          assert(fs->rx_next_pos < fs->rx_len);
          fs->rx_next_seq += fs->rx_ooo_len;

          fs->rx_ooo_len = 0;
        }
      }
    }
#endif
  }

  if ((TCPH_FLAGS(&p->tcp) & TCP_FIN) == TCP_FIN &&
      !(fs->rx_base_sp & FLEXNIC_PL_FLOWST_RXFIN))
  {
    if (fs->rx_next_seq == f_beui32(p->tcp.seqno) + orig_payload && !fs->rx_ooo_len) {
      fin_bump = 1;
      fs->rx_base_sp |= FLEXNIC_PL_FLOWST_RXFIN;
      /* FIN takes up sequence number space */
      fs->rx_next_seq++;
      trigger_ack = 1;
    } else {
      fprintf(stderr, "fast_flows_packet: ignored fin because out of order\n");
    }
  }

unlock:
  /* if we bumped at least one, then we need to add a notification to the
   * queue */
  if (LIKELY(rx_bump != 0 || tx_bump != 0 || fin_bump)) {
#if PL_DEBUG_ARX
    fprintf(stderr, "dma_krx_pkt_fastpath: updating application state\n");
#endif

#ifdef FLEXNIC_TRACING
    struct flextcp_pl_trev_arx te_arx = {
        .rx_bump = rx_bump,
        .tx_bump = tx_bump,

        .db_id = fs->db_id,

        .local_ip = f_beui32(p->ip.dest),
        .remote_ip = f_beui32(p->ip.src),
        .local_port = f_beui16(p->tcp.dest),
        .remote_port = f_beui16(p->tcp.src),
      };
    trace_event(FLEXNIC_PL_TREV_ARX, sizeof(te_arx), &te_arx);
#endif

    uint16_t type;
    type = FLEXTCP_PL_ARX_CONNUPDATE;

    if (fin_bump) {
      type |= FLEXTCP_PL_ARX_FLRXDONE << 8;
    }

    arx_cache_add(ctx, fs->db_id, fs->opaque, rx_bump, rx_pos, tx_bump, type);
  }

  /* Flow control: More receiver space? -> might need to start sending */
  new_avail = tcp_txavail(fs, NULL);
  if (new_avail > old_avail) {
    /* update qman queue */
    if (qman_set(&ctx->qman, flow_id, fs->tx_rate, new_avail -
          old_avail, TCP_MSS, QMAN_SET_RATE | QMAN_SET_MAXCHUNK
          | QMAN_ADD_AVAIL) != 0)
    {
      fprintf(stderr, "fast_flows_packet: qman_set 1 failed, UNEXPECTED\n");
      abort();
    }
  }

  /* if we need to send an ack, also send packet to TX pipeline to do so */
  if (trigger_ack) {
    flow_tx_ack(ctx, fs->tx_next_seq, fs->rx_next_seq, fs->rx_avail,
        fs->tx_next_ts, ts, nbh, opts->ts);
  }

  fs_unlock(fs);
  return trigger_ack;

slowpath:
  if (!no_permanent_sp) {
    fs->rx_base_sp |= FLEXNIC_PL_FLOWST_SLOWPATH;
  }

  fs_unlock(fs);
  /* TODO: should pass current flow state to kernel as well */
  return -1;
}

/* Update receive and transmit queue pointers from application */
int fast_flows_bump(struct dataplane_context *ctx, uint32_t flow_id,
    uint16_t bump_seq, uint32_t rx_bump, uint32_t tx_bump, uint8_t flags,
    struct network_buf_handle *nbh, uint32_t ts)
{
  struct flextcp_pl_flowst *fs = &fp_state->flowst[flow_id];
  uint32_t rx_avail_prev, old_avail, new_avail, tx_avail;
  int ret = -1;

  fs_lock(fs);
#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_atx te_atx = {
      .rx_bump = rx_bump,
      .tx_bump = tx_bump,
      .bump_seq_ent = bump_seq,
      .bump_seq_flow = fs->bump_seq,

      .local_ip = f_beui32(fs->local_ip),
      .remote_ip = f_beui32(fs->remote_ip),
      .local_port = f_beui16(fs->local_port),
      .remote_port = f_beui16(fs->remote_port),

      .flow_id = flow_id,
      .db_id = fs->db_id,

      .tx_next_pos = fs->tx_next_pos,
      .tx_next_seq = fs->tx_next_seq,
      .tx_avail_prev = fs->tx_avail,
      .rx_next_pos = fs->rx_next_pos,
      .rx_avail = fs->rx_avail,
      .tx_len = fs->tx_len,
      .rx_len = fs->rx_len,
      .rx_remote_avail = fs->rx_remote_avail,
      .tx_sent = fs->tx_sent,
    };
  trace_event(FLEXNIC_PL_TREV_ATX, sizeof(te_atx), &te_atx);
#endif

  /* TODO: is this still necessary? */
  /* catch out of order bumps */
  if ((bump_seq >= fs->bump_seq &&
        bump_seq - fs->bump_seq > (UINT16_MAX / 2)) ||
      (bump_seq < fs->bump_seq &&
       (fs->bump_seq < ((UINT16_MAX / 4) * 3) ||
       bump_seq > (UINT16_MAX / 4))))
  {
    goto unlock;
  }
  fs->bump_seq = bump_seq;

  if ((fs->rx_base_sp & FLEXNIC_PL_FLOWST_TXFIN) == FLEXNIC_PL_FLOWST_TXFIN &&
      tx_bump != 0)
  {
    /* TX already closed, don't accept anything for transmission */
    fprintf(stderr, "fast_flows_bump: tx bump while TX is already closed\n");
    tx_bump = 0;
  } else if ((flags & FLEXTCP_PL_ATX_FLTXDONE) == FLEXTCP_PL_ATX_FLTXDONE &&
      !(fs->rx_base_sp & FLEXNIC_PL_FLOWST_TXFIN) &&
      !tx_bump)
  {
    /* Closing TX requires at least one byte (dummy) */
    fprintf(stderr, "fast_flows_bump: tx eos without dummy byte\n");
    goto unlock;
  }

  tx_avail = fs->tx_avail + tx_bump;

  /* calculate how many bytes can be sent before and after this bump */
  old_avail = tcp_txavail(fs, NULL);
  new_avail = tcp_txavail(fs, &tx_avail);

  /* mark connection as closed if requested */
  if ((flags & FLEXTCP_PL_ATX_FLTXDONE) == FLEXTCP_PL_ATX_FLTXDONE &&
      !(fs->rx_base_sp & FLEXNIC_PL_FLOWST_TXFIN))
  {
    fs->rx_base_sp |= FLEXNIC_PL_FLOWST_TXFIN;
  }

  /* update queue manager queue */
  if (old_avail < new_avail) {
    if (qman_set(&ctx->qman, flow_id, fs->tx_rate, new_avail -
          old_avail, TCP_MSS, QMAN_SET_RATE | QMAN_SET_MAXCHUNK
          | QMAN_ADD_AVAIL) != 0)
    {
      fprintf(stderr, "flast_flows_bump: qman_set 1 failed, UNEXPECTED\n");
      abort();
    }
  }

  /* update flow state */
  fs->tx_avail = tx_avail;
  rx_avail_prev = fs->rx_avail;
  fs->rx_avail += rx_bump;

  /* receive buffer freed up from empty, need to send out a window update, if
   * we're not sending anyways. */
  if (new_avail == 0 && rx_avail_prev == 0 && fs->rx_avail != 0) {
    flow_tx_segment(ctx, nbh, fs, fs->tx_next_seq, fs->rx_next_seq,
        fs->rx_avail, 0, 0, fs->tx_next_ts, ts, 0);
    ret = 0;
  }

unlock:
  fs_unlock(fs);
  return ret;
}

/* start retransmitting */
void fast_flows_retransmit(struct dataplane_context *ctx, uint32_t flow_id)
{
  struct flextcp_pl_flowst *fs = &fp_state->flowst[flow_id];
  uint32_t old_avail, new_avail = -1;

  fs_lock(fs);

  /*    uint32_t old_head = fs->tx_head;
      uint32_t old_sent = fs->tx_sent;
      uint32_t old_pos = fs->tx_next_pos;*/

  old_avail = tcp_txavail(fs, NULL);

  if (fs->tx_sent == 0) {
    /*fprintf(stderr, "fast_flows_retransmit: tx sent == 0\n");

      fprintf(stderr, "fast_flows_retransmit: "
          "old_avail=%u new_avail=%u head=%u tx_next_seq=%u old_head=%u "
          "old_sent=%u old_pos=%u new_pos=%u\n", old_avail, new_avail,
          fs->tx_head, fs->tx_next_seq, old_head, old_sent, old_pos,
          fs->tx_next_pos);*/
    goto out;
  }


  flow_reset_retransmit(fs);
  new_avail = tcp_txavail(fs, NULL);

  /*    fprintf(stderr, "fast_flows_retransmit: "
          "old_avail=%u new_avail=%u head=%u tx_next_seq=%u old_head=%u "
          "old_sent=%u old_pos=%u new_pos=%u\n", old_avail, new_avail,
          fs->tx_head, fs->tx_next_seq, old_head, old_sent, old_pos,
          fs->tx_next_pos);*/

  /* update queue manager */
  if (new_avail > old_avail) {
    if (qman_set(&ctx->qman, flow_id, fs->tx_rate, new_avail - old_avail,
          TCP_MSS, QMAN_SET_RATE | QMAN_SET_MAXCHUNK | QMAN_ADD_AVAIL) != 0)
    {
      fprintf(stderr, "flast_flows_bump: qman_set 1 failed, UNEXPECTED\n");
      abort();
    }
  }

out:
  fs_unlock(fs);
  return;
}

/* read `len` bytes from position `pos` in cirucular transmit buffer */
static void flow_tx_read(struct flextcp_pl_flowst *fs, uint32_t pos,
    uint16_t len, void *dst)
{
  uint32_t part;

  if (LIKELY(pos + len <= fs->tx_len)) {
    dma_read(fs->tx_base + pos, len, dst);
  } else {
    part = fs->tx_len - pos;
    dma_read(fs->tx_base + pos, part, dst);
    dma_read(fs->tx_base, len - part, (uint8_t *) dst + part);
  }
}

/* write `len` bytes to position `pos` in cirucular receive buffer */
static void flow_rx_write(struct flextcp_pl_flowst *fs, uint32_t pos,
    uint16_t len, const void *src)
{
  uint32_t part;
  uint64_t rx_base = fs->rx_base_sp & FLEXNIC_PL_FLOWST_RX_MASK;

  if (LIKELY(pos + len <= fs->rx_len)) {
    dma_write(rx_base + pos, len, src);
  } else {
    part = fs->rx_len - pos;
    dma_write(rx_base + pos, part, src);
    dma_write(rx_base, len - part, (const uint8_t *) src + part);
  }
}

#ifdef FLEXNIC_PL_OOO_RECV
static void flow_rx_seq_write(struct flextcp_pl_flowst *fs, uint32_t seq,
    uint16_t len, const void *src)
{
  uint32_t diff = seq - fs->rx_next_seq;
  uint32_t pos = fs->rx_next_pos + diff;
  if (pos >= fs->rx_len)
    pos -= fs->rx_len;
  assert(pos < fs->rx_len);
  flow_rx_write(fs, pos, len, src);
}
#endif

static void flow_tx_segment(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, struct flextcp_pl_flowst *fs,
    uint32_t seq, uint32_t ack, uint32_t rxwnd, uint16_t payload,
    uint32_t payload_pos, uint32_t ts_echo, uint32_t ts_my, uint8_t fin)
{
  uint16_t hdrs_len, optlen, fin_fl;
  struct pkt_tcp *p = network_buf_buf(nbh);
  struct tcp_timestamp_opt *opt_ts;

  /* calculate header length depending on options */
  optlen = (sizeof(*opt_ts) + 3) & ~3;
  hdrs_len = sizeof(*p) + optlen;

  /* fill headers */
  p->eth.dest = fs->remote_mac;
  memcpy(&p->eth.src, &eth_addr, ETH_ADDR_LEN);
  p->eth.type = t_beui16(ETH_TYPE_IP);

  IPH_VHL_SET(&p->ip, 4, 5);
  p->ip._tos = 0;
  p->ip.len = t_beui16(hdrs_len - offsetof(struct pkt_tcp, ip) + payload);
  p->ip.id = t_beui16(3); /* TODO: not sure why we have 3 here */
  p->ip.offset = t_beui16(0);
  p->ip.ttl = 0xff;
  p->ip.proto = IP_PROTO_TCP;
  p->ip.chksum = 0;
  p->ip.src = fs->local_ip;
  p->ip.dest = fs->remote_ip;

  /* mark as ECN capable if flow marked so */
  if ((fs->rx_base_sp & FLEXNIC_PL_FLOWST_ECN) == FLEXNIC_PL_FLOWST_ECN) {
    IPH_ECN_SET(&p->ip, IP_ECN_ECT0);
  }

  fin_fl = (fin ? TCP_FIN : 0);

  p->tcp.src = fs->local_port;
  p->tcp.dest = fs->remote_port;
  p->tcp.seqno = t_beui32(seq);
  p->tcp.ackno = t_beui32(ack);
  TCPH_HDRLEN_FLAGS_SET(&p->tcp, 5 + optlen / 4, TCP_PSH | TCP_ACK | fin_fl);
  p->tcp.wnd = t_beui16(MIN(0xFFFF, rxwnd));
  p->tcp.chksum = 0;
  p->tcp.urgp = t_beui16(0);

  /* fill in timestamp option */
  memset(p + 1, 0, optlen);
  opt_ts = (struct tcp_timestamp_opt *) (p + 1);
  opt_ts->kind = TCP_OPT_TIMESTAMP;
  opt_ts->length = sizeof(*opt_ts);
  opt_ts->ts_val = t_beui32(ts_my);
  opt_ts->ts_ecr = t_beui32(ts_echo);

  /* add payload if requested */
  if (payload > 0) {
    flow_tx_read(fs, payload_pos, payload, (uint8_t *) p + hdrs_len);
  }

  /* checksums */
  tcp_checksums(nbh, p, fs->local_ip, fs->remote_ip, hdrs_len - offsetof(struct
        pkt_tcp, tcp) + payload);

#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_txseg te_txseg = {
      .local_ip = f_beui32(p->ip.src),
      .remote_ip = f_beui32(p->ip.dest),
      .local_port = f_beui16(p->tcp.src),
      .remote_port = f_beui16(p->tcp.dest),

      .flow_seq = seq,
      .flow_ack = ack,
      .flow_flags = TCPH_FLAGS(&p->tcp),
      .flow_len = payload,
    };
  trace_event(FLEXNIC_PL_TREV_TXSEG, sizeof(te_txseg), &te_txseg);
#endif

  tx_send(ctx, nbh, 0, hdrs_len + payload);
}

static void flow_tx_ack(struct dataplane_context *ctx, uint32_t seq,
    uint32_t ack, uint32_t rxwnd, uint32_t echots, uint32_t myts,
    struct network_buf_handle *nbh, struct tcp_timestamp_opt *ts_opt)
{
  struct pkt_tcp *p;
  struct eth_addr eth;
  ip_addr_t ip;
  beui16_t port;
  uint16_t hdrlen;
  uint16_t ecn_flags = 0;

  p = network_buf_bufoff(nbh);

#if PL_DEBUG_TCPACK
  fprintf(stderr, "FLOW local=%08x:%05u remote=%08x:%05u ACK: seq=%u ack=%u\n",
      f_beui32(p->ip.dest), f_beui16(p->tcp.dest),
      f_beui32(p->ip.src), f_beui16(p->tcp.src), seq, ack);
#endif

  /* swap addresses */
  eth = p->eth.src;
  p->eth.src = p->eth.dest;
  p->eth.dest = eth;
  ip = p->ip.src;
  p->ip.src = p->ip.dest;
  p->ip.dest = ip;
  port = p->tcp.src;
  p->tcp.src = p->tcp.dest;
  p->tcp.dest = port;

  hdrlen = sizeof(*p) + (TCPH_HDRLEN(&p->tcp) - 5) * 4;

  /* If ECN flagged, set TCP response flag */
  if (IPH_ECN(&p->ip) == IP_ECN_CE) {
    ecn_flags = TCP_ECE;
  }

  /* mark ACKs as ECN in-capable */
  IPH_ECN_SET(&p->ip, IP_ECN_NONE);

  /* change TCP header to ACK */
  p->tcp.seqno = t_beui32(seq);
  p->tcp.ackno = t_beui32(ack);
  TCPH_HDRLEN_FLAGS_SET(&p->tcp, TCPH_HDRLEN(&p->tcp), TCP_ACK | ecn_flags);
  p->tcp.wnd = t_beui16(MIN(0xFFFF, rxwnd));
  p->tcp.urgp = t_beui16(0);

  /* fill in timestamp option */
  ts_opt->ts_val = t_beui32(myts);
  ts_opt->ts_ecr = t_beui32(echots);

  p->ip.len = t_beui16(hdrlen - offsetof(struct pkt_tcp, ip));
  p->ip.ttl = 0xff;

  /* checksums */
  tcp_checksums(nbh, p, p->ip.src, p->ip.dest, hdrlen - offsetof(struct
        pkt_tcp, tcp));

#ifdef FLEXNIC_TRACING
  struct flextcp_pl_trev_txack te_txack = {
      .local_ip = f_beui32(p->ip.src),
      .remote_ip = f_beui32(p->ip.dest),
      .local_port = f_beui16(p->tcp.src),
      .remote_port = f_beui16(p->tcp.dest),

      .flow_seq = seq,
      .flow_ack = ack,
      .flow_flags = TCPH_FLAGS(&p->tcp),
    };
  trace_event(FLEXNIC_PL_TREV_TXACK, sizeof(te_txack), &te_txack);
#endif

  tx_send(ctx, nbh, network_buf_off(nbh), hdrlen);
}

static void flow_reset_retransmit(struct flextcp_pl_flowst *fs)
{
  uint32_t x;

  /* reset flow state as if we never transmitted those segments */
  fs->rx_dupack_cnt = 0;

  fs->tx_next_seq -= fs->tx_sent;
  if (fs->tx_next_pos >= fs->tx_sent) {
    fs->tx_next_pos -= fs->tx_sent;
  } else {
    x = fs->tx_sent - fs->tx_next_pos;
    fs->tx_next_pos = fs->tx_len - x;
  }
  fs->tx_sent = 0;

  /* cut rate by half if first drop in control interval */
  if (fs->cnt_tx_drops == 0) {
    fs->tx_rate /= 2;
  }

  fs->cnt_tx_drops++;
}

static inline void tcp_checksums(struct network_buf_handle *nbh,
    struct pkt_tcp *p, beui32_t ip_s, beui32_t ip_d, uint16_t l3_paylen)
{
  p->ip.chksum = 0;
#ifdef HWXSUM_EN
  p->tcp.chksum = tx_xsum_enable(nbh, &p->ip, ip_s, ip_d, l3_paylen);
#else
  p->tcp.chksum = 0;
  p->ip.chksum = rte_ipv4_cksum((void *) &p->ip);
  p->tcp.chksum = rte_ipv4_udptcp_cksum((void *) &p->ip, (void *) &p->tcp);
#endif
}

void fast_flows_kernelxsums(struct network_buf_handle *nbh,
    struct pkt_tcp *p)
{
  tcp_checksums(nbh, p, p->ip.src, p->ip.dest,
      f_beui16(p->ip.len) - sizeof(p->ip));
}

static inline uint32_t flow_hash(struct flow_key *k)
{
  return crc32c_sse42_u32(k->local_port.x | (((uint32_t) k->remote_port.x) << 16),
      crc32c_sse42_u64(k->local_ip.x | (((uint64_t) k->remote_ip.x) << 32), 0));
}

void fast_flows_packet_fss(struct dataplane_context *ctx,
    struct network_buf_handle **nbhs, void **fss, uint16_t n)
{
  uint32_t hashes[n];
  uint32_t h, k, j, eh, fid, ffid;
  uint16_t i;
  struct pkt_tcp *p;
  struct flow_key key;
  struct flextcp_pl_flowhte *e;
  struct flextcp_pl_flowst *fs;

  /* calculate hashes and prefetch hash table buckets */
  for (i = 0; i < n; i++) {
    p = network_buf_bufoff(nbhs[i]);

    key.local_ip = p->ip.dest;
    key.remote_ip = p->ip.src;
    key.local_port = p->tcp.dest;
    key.remote_port = p->tcp.src;
    h = flow_hash(&key);

    rte_prefetch0(&fp_state->flowht[h % FLEXNIC_PL_FLOWHT_ENTRIES]);
    rte_prefetch0(&fp_state->flowht[(h + 3) % FLEXNIC_PL_FLOWHT_ENTRIES]);
    hashes[i] = h;
  }

  /* prefetch flow state for buckets with matching hashes
   * (usually 1 per packet, except in case of collisions) */
  for (i = 0; i < n; i++) {
    h = hashes[i];
    for (j = 0; j < FLEXNIC_PL_FLOWHT_NBSZ; j++) {
      k = (h + j) % FLEXNIC_PL_FLOWHT_ENTRIES;
      e = &fp_state->flowht[k];

      ffid = e->flow_id;
      MEM_BARRIER();
      eh = e->flow_hash;

      fid = ffid & ((1 << FLEXNIC_PL_FLOWHTE_POSSHIFT) - 1);
      if ((ffid & FLEXNIC_PL_FLOWHTE_VALID) == 0 || eh != h) {
        continue;
      }

      rte_prefetch0(&fp_state->flowst[fid]);
    }
  }

  /* finish hash table lookup by checking 5-tuple in flow state */
  for (i = 0; i < n; i++) {
    p = network_buf_bufoff(nbhs[i]);
    fss[i] = NULL;
    h = hashes[i];

    for (j = 0; j < FLEXNIC_PL_FLOWHT_NBSZ; j++) {
      k = (h + j) % FLEXNIC_PL_FLOWHT_ENTRIES;
      e = &fp_state->flowht[k];

      ffid = e->flow_id;
      MEM_BARRIER();
      eh = e->flow_hash;

      fid = ffid & ((1 << FLEXNIC_PL_FLOWHTE_POSSHIFT) - 1);
      if ((ffid & FLEXNIC_PL_FLOWHTE_VALID) == 0 || eh != h) {
        continue;
      }

      MEM_BARRIER();
      fs = &fp_state->flowst[fid];
      if ((fs->local_ip.x == p->ip.dest.x) &
          (fs->remote_ip.x == p->ip.src.x) &
          (fs->local_port.x == p->tcp.dest.x) &
          (fs->remote_port.x == p->tcp.src.x))
      {
        rte_prefetch0((uint8_t *) fs + 64);
        fss[i] = &fp_state->flowst[fid];
        break;
      }
    }
  }
}
