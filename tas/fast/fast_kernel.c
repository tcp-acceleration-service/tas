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
#include <unistd.h>
#include <rte_config.h>

#include <tas_memif.h>

#include "internal.h"
#include "fastemu.h"
#include "tcp_common.h"


static inline void inject_tcp_ts(void *buf, uint16_t len, uint32_t ts,
    struct network_buf_handle *nbh);

int fast_kernel_poll(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, uint32_t ts)
{
  void *buf = network_buf_buf(nbh);
  struct flextcp_pl_appctx *kctx = &fp_state->kctx[ctx->id];
  struct flextcp_pl_ktx *ktx;
  uint32_t flow_id, len;
  int ret = -1;

  /* stop if context is not in use */
  if (kctx->tx_len == 0)
    return -1;

  ktx = dma_pointer(kctx->tx_base + kctx->tx_head, sizeof(*ktx));

  if (ktx->type == 0) {
    return -1;
  } else if (ktx->type == FLEXTCP_PL_KTX_PACKET) {
    len = ktx->msg.packet.len;

    /* Read transmit queue entry */
    dma_read(ktx->msg.packet.addr, len, buf);

    ret = 0;
    inject_tcp_ts(buf, len, ts, nbh);
    tx_send(ctx, nbh, 0, len);
  } else if (ktx->type == FLEXTCP_PL_KTX_PACKET_NOTS) {
    /* send packet without filling in timestamp */
    len = ktx->msg.packet.len;

    /* Read transmit queue entry */
    dma_read(ktx->msg.packet.addr, len, buf);

    ret = 0;
    tx_send(ctx, nbh, 0, len);
  } else if (ktx->type == FLEXTCP_PL_KTX_CONNRETRAN) {
    flow_id = ktx->msg.connretran.flow_id;
    if (flow_id >= FLEXNIC_PL_FLOWST_NUM) {
      fprintf(stderr, "fast_kernel_qman: invalid flow id=%u\n", flow_id);
      abort();
    }

    fast_flows_retransmit(ctx, flow_id);
    ret = 1;
  } else {
    fprintf(stderr, "fast_appctx_poll: unknown type: %u\n", ktx->type);
    abort();
  }

  MEM_BARRIER();
  ktx->type = 0;

  kctx->tx_head += sizeof(*ktx);
  if (kctx->tx_head >= kctx->tx_len)
    kctx->tx_head -= kctx->tx_len;

  return ret;
}

void fast_kernel_packet(struct dataplane_context *ctx,
    struct network_buf_handle *nbh)
{
  struct flextcp_pl_appctx *kctx = &fp_state->kctx[ctx->id];
  struct flextcp_pl_krx *krx;
  uint16_t len;

  /* queue not initialized yet */
  if (kctx->rx_len == 0) {
    return;
  }

  krx = dma_pointer(kctx->rx_base + kctx->rx_head, sizeof(*krx));

  /* queue full */
  if (krx->type != 0) {
    ctx->kernel_drop++;
    return;
  }

  kctx->rx_head += sizeof(*krx);
  if (kctx->rx_head >= kctx->rx_len)
    kctx->rx_head -= kctx->rx_len;


  len = network_buf_len(nbh);
  dma_write(krx->addr, len, network_buf_bufoff(nbh));

  if (network_buf_flowgroup(nbh, &krx->msg.packet.flow_group)) {
    fprintf(stderr, "fast_kernel_packet: network_buf_flowgroup failed\n");
    abort();
  }

  krx->msg.packet.len = len;
  krx->msg.packet.fn_core = ctx->id;
  MEM_BARRIER();

  /* krx queue header */
  krx->type = FLEXTCP_PL_KRX_PACKET;
  notify_slowpath_core();
}

static inline void inject_tcp_ts(void *buf, uint16_t len, uint32_t ts,
    struct network_buf_handle *nbh)
{
  struct pkt_tcp *p = buf;
  struct tcp_opts opts;

  if (len < sizeof(*p) || f_beui16(p->eth.type) != ETH_TYPE_IP ||
      p->ip.proto != IP_PROTO_TCP)
  {
    return;
  }

  if (tcp_parse_options(buf, len, &opts) != 0) {
    fprintf(stderr, "inject_tcp_ts: parsing options failed\n");
    return;
  }

  if (opts.ts == NULL) {
    return;
  }

  opts.ts->ts_val = t_beui32(ts);

  fast_flows_kernelxsums(nbh, p);
}
