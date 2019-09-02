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
#include <inttypes.h>
#include <string.h>

#include <tas_ll_connect.h>
#include <tas_memif.h>

struct flextcp_pl_mem *plm;

/** connect to flexnic shared memory regions */
static int connect_flexnic(void)
{
  struct flexnic_info *info;
  void *mem_start, *int_mem_start;

  if (flexnic_driver_connect(&info, &mem_start) != 0) {
    fprintf(stderr, "flexnic_driver_connect failed\n");
    return -1;
  }

  if (flexnic_driver_internal(&int_mem_start) != 0) {
    fprintf(stderr, "flexnic_driver_internal failed\n");
    return -1;
  }
  plm = int_mem_start;

  if (info->internal_mem_size < sizeof(*plm)) {
    fprintf(stderr, "internal memory smaller than expected\n");
    return -1;
  }

  return 0;
}

static int dump_appctx(uint16_t db_id)
{
#if 0
  struct flextcp_pl_appctx *ctx;

  if (db_id >= FLEXNIC_PL_APPCTX_NUM) {
    fprintf(stderr, "dump_appctx: invalid doorbell id %u\n", db_id);
    return -1;
  }

  if (db_id == 0) {
    ctx = &plm->kctx;
  } else {
    ctx = &plm->appctx[db_id];
  }

  /* skip contexts without receive and transmit queues */
  if (ctx->rx_len == 0 && ctx->tx_len == 0) {
    return 0;
  }

  printf("context %u {\n"
         "  qman_qid=%03u\n"
         "  appst_id=%03u\n"
         "  rx {\n"
         "         base=%016"PRIx64"\n"
         "          len=%08x\n"
         "         head=%08x\n"
         "         tail=%08x\n"
         "  }\n"
         "  tx {\n"
         "         base=%016"PRIx64"\n"
         "          len=%08x\n"
         "         head=%08x\n"
         "    head_last=%08x\n"
         "         tail=%08x\n"
         "  }\n", db_id, ctx->qman_qid, ctx->appst_id, ctx->rx_base,
         ctx->rx_len, ctx->rx_head, ctx->rx_tail, ctx->tx_base, ctx->tx_len,
         ctx->tx_head, ctx->tx_head_last, ctx->tx_tail);
#endif
  return 0;
}

static int dump_flow(uint32_t flow_id)
{
  struct flextcp_pl_flowst *fs;
  uint64_t mac = 0;

  if (flow_id >= FLEXNIC_PL_FLOWST_NUM) {
    fprintf(stderr, "dump_appctx: invalid doorbell id %u\n", flow_id);
    return -1;
  }

  fs = &plm->flowst[flow_id];

  /* skip flows without receive and transmit buffers */
  if (fs->rx_len == 0 && fs->tx_len == 0) {
    return 0;
  }

  memcpy(&mac, &fs->remote_mac, 6);
  printf("flow %u {\n"
         "  opaque=%016"PRIx64"\n"
         "  db_id=%03u\n"
         "  flag_slowpath=%u\n"
         "  flag_ecn=%u\n"
         "  flag_txfin=%u\n"
         "  flag_rxfin=%u\n"
         "  bump_seq=%010u\n"
         "  addr {\n"
         "       local_ip=%08x\n"
         "     local_port=%05u\n"
         "      remote_ip=%08x\n"
         "    remote_port=%05u\n"
         "     remote_mac=%012"PRIx64"\n"
         "  }\n"
         "  rx {\n"
         "            base=%016llx\n"
         "             len=%08x\n"
         "           avail=%08x\n"
         "    remote_avail=%08x\n"
         "        next_pos=%08x\n"
         "        next_seq=%010u\n"
         "      dupack_cnt=%08x\n"
#ifdef FLEXNIC_PL_OOO_RECV
         "       ooo_start=%08x\n"
         "         ooo_len=%08x\n"
#endif
         "  }\n"
         "  tx {\n"
         "            base=%016"PRIx64"\n"
         "             len=%08x\n"
         "           avail=%08x\n"
         "            sent=%08x\n"
         "        next_pos=%08x\n"
         "        next_seq=%010u\n"
         "         next_ts=%08x\n"
         "  }\n"
         "  cc {\n"
         "         tx_rate=%10u\n"
         "        tx_drops=%10u\n"
         "         rx_acks=%10u\n"
         "    rx_ack_bytes=%10u\n"
         "    rx_ecn_bytes=%10u\n"
         "         rtt_est=%10u\n"
         "  }\n"
         "}\n", flow_id, fs->opaque, fs->db_id,
      !!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_SLOWPATH),
      !!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_ECN),
      !!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_TXFIN),
      !!(fs->rx_base_sp & FLEXNIC_PL_FLOWST_RXFIN),
      fs->bump_seq,
      f_beui32(fs->local_ip), f_beui16(fs->local_port), f_beui32(fs->remote_ip),
      f_beui16(fs->remote_port), mac,
      (fs->rx_base_sp & FLEXNIC_PL_FLOWST_RX_MASK), fs->rx_len, fs->rx_avail,
      fs->rx_remote_avail, fs->rx_next_pos, fs->rx_next_seq, fs->rx_dupack_cnt,
#ifdef FLEXNIC_PL_OOO_RECV
      fs->rx_ooo_start, fs->rx_ooo_len,
#endif
      fs->tx_base, fs->tx_len, fs->tx_avail, fs->tx_sent, fs->tx_next_pos,
      fs->tx_next_seq, fs->tx_next_ts,
      fs->tx_rate, fs->cnt_tx_drops, fs->cnt_rx_acks, fs->cnt_rx_ack_bytes,
      fs->cnt_rx_ecn_bytes, fs->rtt_est);

  return 0;
}

int main(int argc, char *argv[])
{
  uint32_t i;

  if (connect_flexnic() != 0) {
    return EXIT_FAILURE;
  }

  for (i = 0; i < FLEXNIC_PL_APPCTX_NUM; i++) {
    dump_appctx(i);
  }
  for (i = 0; i < FLEXNIC_PL_FLOWST_NUM; i++) {
    dump_flow(i);
  }


  return EXIT_SUCCESS;
}
