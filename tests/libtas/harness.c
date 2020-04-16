#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "../testutils.h"
#include <tas_ll.h>
#include <tas_memif.h>

struct harness_fpc_ctx {
  struct flextcp_pl_atx *atx_base;
  size_t atx_pos;

  struct flextcp_pl_arx *arx_base;
  size_t arx_pos;
};

struct harness_ctx {
  struct kernel_appout *aout_base;
  size_t aout_len;
  size_t aout_pos;

  struct kernel_appin *ain_base;
  size_t ain_len;
  size_t ain_pos;

  size_t atx_len;
  size_t arx_len;

  struct harness_fpc_ctx *fpcs;
};

struct harness {
  size_t num_ctxs;
  size_t num_fpcores;
  size_t next_ctx;
  uint64_t num_kicks;
  struct harness_ctx *ctxs;
};

struct harness_params harness_param;
struct harness harness;

void harness_prepare(struct harness_params *hp)
{
  size_t i, j;
  struct harness_ctx *hc;
  struct harness_fpc_ctx *hf;

  harness.num_ctxs = hp->num_ctxs;
  harness.num_fpcores = hp->fp_cores;
  harness.next_ctx = 0;
  harness.num_kicks = 0;

  /* allocate contexts */
  harness.ctxs = test_zalloc(harness.num_ctxs * sizeof(*hc));

  for (i = 0; i < hp->num_ctxs; i++) {
    hc = &harness.ctxs[i];

    hc->fpcs = test_zalloc(harness.num_fpcores * sizeof(*hf));
    hc->aout_base = test_zalloc(hp->aout_len * sizeof(*hc->aout_base));
    hc->aout_len = hp->aout_len;
    hc->aout_pos = 0;
    hc->ain_base = test_zalloc(hp->ain_len * sizeof(*hc->ain_base));
    hc->ain_len = hp->ain_len;
    hc->ain_pos = 0;
    hc->atx_len = hp->atx_len;
    hc->arx_len = hp->arx_len;

    for (j = 0; j < harness.num_fpcores; j++) {
      hf = &hc->fpcs[j];
      hf->atx_base = test_zalloc(hp->atx_len * sizeof(*hf->atx_base));
      hf->arx_base = test_zalloc(hp->arx_len * sizeof(*hf->arx_base));
      hf->atx_pos = 0;
      hf->arx_pos = 0;
    }
  }
}

int harness_aout_peek(struct kernel_appout **p_ao, size_t ctxid)
{
  struct harness_ctx *hc = &harness.ctxs[ctxid];
  struct kernel_appout *ao = &hc->aout_base[hc->aout_pos];

  if (ao->type == KERNEL_APPOUT_INVALID)
    return -1;

  *p_ao = ao;
  return 0;
}

int harness_aout_pop(size_t ctxid)
{
  struct harness_ctx *hc = &harness.ctxs[ctxid];
  struct kernel_appout *ao = &hc->aout_base[hc->aout_pos];

  if (ao->type == KERNEL_APPOUT_INVALID)
    return -1;

  ao->type = 0;

  hc->aout_pos++;
  if (hc->aout_pos >= hc->aout_len)
    hc->aout_pos -= hc->aout_len;
  return 0;
}

int harness_aout_pull_connopen(size_t ctxid, uint64_t opaque, uint32_t remote_ip,
    uint16_t remote_port, uint8_t flags)
{
  struct harness_ctx *hc = &harness.ctxs[ctxid];
  struct kernel_appout *pao;
  struct kernel_appout_conn_open *aco;

  if (harness_aout_peek(&pao, 0) != 0)
    return -1;

  aco = &pao->data.conn_open;
  if (pao->type == KERNEL_APPOUT_CONN_OPEN &&
      aco->opaque == opaque &&
      aco->remote_ip == remote_ip &&
      aco->remote_port == remote_port &&
      aco->flags == flags)
  {
    pao->type = 0;

    hc->aout_pos++;
    if (hc->aout_pos >= hc->aout_len)
      hc->aout_pos -= hc->aout_len;

    return 0;
  } else {
    return 1;
  }
}

int harness_aout_pull_connopen_op(size_t ctxid, uint64_t *opaque,
    uint32_t remote_ip, uint16_t remote_port, uint8_t flags)
{
  struct harness_ctx *hc = &harness.ctxs[ctxid];
  struct kernel_appout *pao;
  struct kernel_appout_conn_open *aco;

  if (harness_aout_peek(&pao, 0) != 0)
    return -1;

  aco = &pao->data.conn_open;
  if (pao->type == KERNEL_APPOUT_CONN_OPEN &&
      aco->remote_ip == remote_ip &&
      aco->remote_port == remote_port &&
      aco->flags == flags)
  {
    *opaque = aco->opaque;
    pao->type = 0;

    hc->aout_pos++;
    if (hc->aout_pos >= hc->aout_len)
      hc->aout_pos -= hc->aout_len;

    return 0;
  } else {
    return 1;
  }
}

int harness_ain_push(size_t ctxid, struct kernel_appin *ai)
{
  struct harness_ctx *hc = &harness.ctxs[ctxid];
  struct kernel_appin *pai = &hc->ain_base[hc->ain_pos];

  if (pai->type != KERNEL_APPIN_INVALID)
    return -1;

  *pai = *ai;

  hc->ain_pos++;
  if (hc->ain_pos >= hc->ain_len)
    hc->ain_pos -= hc->ain_len;

  return 0;
}

int harness_ain_push_connopened(size_t ctxid, uint64_t opaque, uint32_t rx_len,
    void *rx_buf, uint32_t tx_len, void *tx_buf, uint32_t flow_id,
    uint32_t local_ip, uint16_t local_port, uint32_t core)
{
  struct kernel_appin ai;
  struct kernel_appin_conn_opened *aico;

  ai.type = KERNEL_APPIN_CONN_OPENED;
  aico = &ai.data.conn_opened;
  aico->opaque = opaque;
  aico->rx_len = rx_len;
  aico->rx_off = (uintptr_t) rx_buf;
  aico->tx_len = tx_len;
  aico->tx_off = (uintptr_t) tx_buf;
  aico->status = 0;
  aico->seq_rx = 2;
  aico->seq_tx = 2;
  aico->flow_id = flow_id;
  aico->local_ip = local_ip;
  aico->local_port = local_port;
  aico->fn_core = core;

  return harness_ain_push(ctxid, &ai);
}

int harness_ain_push_connopen_failed(size_t ctxid, uint64_t opaque,
    int32_t status)
{
  struct kernel_appin ai;
  struct kernel_appin_conn_opened *aico;

  memset(&ai, 0, sizeof(ai));
  ai.type = KERNEL_APPIN_CONN_OPENED;
  aico = &ai.data.conn_opened;
  aico->opaque = opaque;
  aico->status = status;

  return harness_ain_push(ctxid, &ai);
}

int harness_atx_pull(size_t ctxid, size_t qid, uint32_t rx_bump,
    uint32_t tx_bump, uint32_t flow_id, uint16_t bump_seq, uint8_t flags)
{
  struct harness_ctx *hc = &harness.ctxs[ctxid];
  struct harness_fpc_ctx *fpc = &hc->fpcs[qid];
  struct flextcp_pl_atx *atx = &fpc->atx_base[fpc->atx_pos];

  if (atx->type == 0)
    return -1;


  if (atx->type == FLEXTCP_PL_ATX_CONNUPDATE &&
      atx->msg.connupdate.rx_bump == rx_bump &&
      atx->msg.connupdate.tx_bump == tx_bump &&
      atx->msg.connupdate.flow_id == flow_id &&
      atx->msg.connupdate.bump_seq == bump_seq &&
      atx->msg.connupdate.flags == flags)
  {
    atx->type = 0;

    fpc->atx_pos++;
    if (fpc->atx_pos >= hc->atx_len)
      fpc->atx_pos -= hc->atx_len;

    return 0;
  } else {
    return 1;
  }
}

int harness_arx_push(size_t ctxid, size_t qid, uint64_t opaque,
    uint32_t rx_bump, uint32_t rx_pos, uint32_t tx_bump, uint8_t flags)
{
  struct harness_ctx *hc = &harness.ctxs[ctxid];
  struct harness_fpc_ctx *fpc = &hc->fpcs[qid];
  struct flextcp_pl_arx *arx = &fpc->arx_base[fpc->arx_pos];

  if (arx->type != FLEXTCP_PL_ARX_INVALID)
    return -1;

  arx->msg.connupdate.opaque = opaque;
  arx->msg.connupdate.rx_bump = rx_bump;
  arx->msg.connupdate.rx_pos = rx_pos;
  arx->msg.connupdate.tx_bump = tx_bump;
  arx->msg.connupdate.flags = flags;
  arx->type = FLEXTCP_PL_ARX_CONNUPDATE;

  fpc->arx_pos++;
  if (fpc->arx_pos >= hc->arx_len)
    fpc->arx_pos -= hc->arx_len;

  return 0;

}

int flextcp_kernel_connect(void)
{
  return 0;
}

void flextcp_kernel_kick(void)
{
  harness.num_kicks++;
}

int flexnic_driver_internal(void **int_mem_start)
{
  return -1;
}

int flexnic_driver_connect(struct flexnic_info **p_info, void **p_mem_start)
{
  static struct flexnic_info info;
  memset(&info, 0, sizeof(info));

  *p_info = &info;
  /* hack: set mem start to 0 so we can just use pointers as offsets */
  *p_mem_start = (void *) 0;
  return 0;
}

int flextcp_kernel_newctx(struct flextcp_context *ctx)
{
  size_t i;
  struct harness_ctx *hc = &harness.ctxs[harness.next_ctx];
  if (harness.next_ctx >= harness.num_ctxs) {
    printf("flextcp_kernel_newctx: not enough contexts\n");
    return -1;
  }
  harness.next_ctx++;

  /* fill in ctx struct */
  ctx->kin_base = (uint8_t *) hc->aout_base;
  ctx->kin_len = hc->aout_len;
  ctx->kin_head = 0;

  ctx->kout_base = (uint8_t *) hc->ain_base;
  ctx->kout_len = hc->ain_len;
  ctx->kout_head = 0;

  ctx->db_id = 0; /* todo */
  ctx->num_queues = harness.num_fpcores;
  ctx->next_queue = 0;

  ctx->rxq_len = hc->arx_len;
  ctx->txq_len = hc->atx_len;

  for (i = 0; i < ctx->num_queues; i++) {
    ctx->queues[i].rxq_base =
      (uint8_t *) hc->fpcs[i].arx_base;
    ctx->queues[i].txq_base =
      (uint8_t *) hc->fpcs[i].atx_base;

    ctx->queues[i].rxq_head = 0;
    ctx->queues[i].txq_tail = 0;
    ctx->queues[i].txq_avail = ctx->txq_len;
    ctx->queues[i].last_ts = 0;
  }

  return 0;
}
