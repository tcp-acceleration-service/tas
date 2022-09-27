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

#include <tas_memif.h>

#include "internal.h"
#include "fastemu.h"

static void fast_appctx_poll_pf(struct dataplane_context *ctx, uint32_t id, 
    uint16_t app_id);
static int fast_appctx_poll_fetch(struct dataplane_context *ctx, uint32_t actx_id,
    uint16_t app_id, void **pqe);
static int fast_actx_rxq_probe(struct dataplane_context *ctx, uint32_t id,
    uint16_t app_id);void fast_appctx_poll_pf_active(struct dataplane_context *ctx)
{
  uint32_t cid, aid;
  struct polled_app *act_app;

  aid = ctx->act_head;
  do {
    act_app = &ctx->polled_apps[aid];

    cid = act_app->act_ctx_head;
    do {
      fast_appctx_poll_pf(ctx, cid, aid);
      cid = act_app->ctxs[cid].next;
    } while(cid != act_app->act_ctx_head);
    
    aid = ctx->polled_apps[aid].next;
  } while (aid != ctx->act_head);
} 

void fast_appctx_poll_pf_all(struct dataplane_context *ctx)
{
  unsigned int i, j;
  uint32_t aid, cid;
  for  (i = 0; i < FLEXNIC_PL_APPST_NUM; i++)
  {
    for (j = 0; j < FLEXNIC_PL_APPCTX_NUM; j++) 
    {
      aid = (ctx->poll_next_app + i) % FLEXNIC_PL_APPST_NUM;
      cid = (ctx->polled_apps[aid].poll_next_ctx + j) % FLEXNIC_PL_APPCTX_NUM;
      fast_appctx_poll_pf(ctx, cid, aid);
    }
  }
}

static void fast_appctx_poll_pf(struct dataplane_context *ctx, uint32_t id, 
    uint16_t app_id)
{
  struct flextcp_pl_appctx *actx = &fp_state->appctx[ctx->id][app_id][id];
  rte_prefetch0(dma_pointer(actx->tx_base + actx->tx_head, 1));
}

int fast_appctx_poll_fetch_active(struct dataplane_context *ctx, uint16_t max,
    unsigned *total, int *n_rem, struct polled_context *rem_ctxs[BATCH_SIZE], 
    void *aqes[BATCH_SIZE])
{
  int ret;
  unsigned i_b;
  uint16_t k = 0;
  uint32_t aid, cid;
  struct polled_app *act_app;
  struct polled_context *act_ctx;
  
  aid = ctx->act_head;
  do {
    act_app = &ctx->polled_apps[aid];

    cid = act_app->act_ctx_head;
    do  {
      act_ctx = &act_app->ctxs[cid];
      for (i_b = 0; i_b < BATCH_SIZE && k < max; i_b++) 
      {
        ret = fast_appctx_poll_fetch(ctx, cid, aid, &aqes[k]);
        if (ret == 0)
        {
          k++;
          act_ctx->null_rounds = 0;
        } else
        {
          act_ctx->null_rounds = act_ctx->null_rounds == MAX_NULL_ROUNDS ? 
              MAX_NULL_ROUNDS : act_ctx->null_rounds + 1;
          if (act_ctx->null_rounds >= MAX_NULL_ROUNDS)
          {
            rem_ctxs[*n_rem] = act_ctx;
            *n_rem = *n_rem + 1;
          }
          break;
        }
        *total = *total + 1;
      }
      cid = act_ctx->next;
    } while(cid != act_app->act_ctx_head && k < max);

    act_app->act_ctx_head = act_app->ctxs[act_app->act_ctx_head].next;
    act_app->act_ctx_tail = act_app->ctxs[act_app->act_ctx_tail].next;

    aid = ctx->polled_apps[aid].next; 
  } while (aid != ctx->act_head && k < max);

  return k;
}

int fast_appctx_poll_fetch_all(struct dataplane_context *ctx, uint16_t max,
    unsigned *total, void *aqes[BATCH_SIZE])
{
  int ret;
  unsigned i_a, i_c, i_b;
  uint16_t k = 0;
  uint32_t next_app, next_ctx;
  struct polled_app *p_app;
  struct polled_context *p_ctx;

  for (i_a = 0; i_a < FLEXNIC_PL_APPST_NUM && k < max; i_a++)
  {
    next_app = ctx->poll_next_app;
    p_app = &ctx->polled_apps[next_app];
    for (i_c = 0; i_c < FLEXNIC_PL_APPCTX_NUM && k < max; i_c++) 
    {
      next_ctx = p_app->poll_next_ctx;
      p_ctx = &p_app->ctxs[next_ctx];
      for (i_b = 0; i_b < BATCH_SIZE && k < max; i_b++) 
      {
        ret = fast_appctx_poll_fetch(ctx, next_ctx, next_app, &aqes[k]);
        if (ret == 0) 
        {
          p_ctx->null_rounds = 0;
          
          /* Add app to active list if it is not already in list */
          if ((p_app->flags & FLAG_ACTIVE) == 0)
          {
            enqueue_app_to_active(ctx, next_app);
          }

          /* Add app ctx to active list if it is not already in list */
          if ((p_ctx->flags & FLAG_ACTIVE) == 0)
          {
            enqueue_ctx_to_active(p_app, next_ctx);
          }

          k++;
        } else
        {
          p_ctx->null_rounds = p_ctx->null_rounds == MAX_NULL_ROUNDS ? 
              MAX_NULL_ROUNDS : p_ctx->null_rounds + 1;
          break;
        }

        *total = *total + 1;
      }
      p_app->poll_next_ctx = (next_ctx + 1) % FLEXNIC_PL_APPCTX_NUM;
    }
    ctx->poll_next_app = (next_app + 1) % FLEXNIC_PL_APPST_NUM;
  }

  return k;
}

static int fast_appctx_poll_fetch(struct dataplane_context *ctx, uint32_t actx_id,
    uint16_t app_id, void **pqe)
{
  struct flextcp_pl_appctx *actx = &fp_state->appctx[ctx->id][app_id][actx_id];
  struct flextcp_pl_atx *atx;
  uint8_t type;
  uint32_t flow_id  = -1;

  /* stop if context is not in use */
  if (actx->tx_len == 0)
    return -1;

  atx = dma_pointer(actx->tx_base + actx->tx_head, sizeof(*atx));

  type = atx->type;
  MEM_BARRIER();

  if (type == 0) {
    return -1;
  } else if (type != FLEXTCP_PL_ATX_CONNUPDATE) {
    fprintf(stderr, "fast_appctx_poll: unknown type: %u id=%u\n", type,
        actx_id);
    abort();
  }

  *pqe = atx;

  /* update RX/TX queue pointers for connection */
  flow_id = atx->msg.connupdate.flow_id;
  if (flow_id >= FLEXNIC_PL_FLOWST_NUM) {
    fprintf(stderr, "fast_appctx_poll: invalid flow id=%u\n", flow_id);
    abort();
  }

  void *fs = &fp_state->flowst[flow_id];
  rte_prefetch0(fs);
  rte_prefetch0(fs + 64);
 
  actx->tx_head += sizeof(*atx);
  if (actx->tx_head >= actx->tx_len)
    actx->tx_head -= actx->tx_len;

  return 0;
}

int fast_appctx_poll_bump(struct dataplane_context *ctx, void *pqe,
    struct network_buf_handle *nbh, uint32_t ts)
{
  struct flextcp_pl_atx *atx = pqe;
  int ret;

  ret = fast_flows_bump(ctx, atx->msg.connupdate.flow_id,
      atx->msg.connupdate.bump_seq, atx->msg.connupdate.rx_bump,
      atx->msg.connupdate.tx_bump, atx->msg.connupdate.flags, nbh, ts);

  if (ret != 0)
    ret = 1;

  MEM_BARRIER();
  atx->type = 0;

  return ret;
}

void fast_actx_rxq_pf(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx)
{

  rte_prefetch0(dma_pointer(actx->rx_base + actx->rx_head,
        sizeof(struct flextcp_pl_arx)));
}

int fast_actx_rxq_alloc(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_arx **arx)
{
  struct flextcp_pl_arx *parx;
  uint32_t rxnhead;
  int ret = 0;

  if (actx->rx_avail == 0) {
    return -1;
  }

  MEM_BARRIER();
  parx = dma_pointer(actx->rx_base + actx->rx_head, sizeof(*parx));

  rxnhead = actx->rx_head + sizeof(*parx);
  if (rxnhead >= actx->rx_len) {
    rxnhead -= actx->rx_len;
  }
  actx->rx_head = rxnhead;
  actx->rx_avail -= sizeof(*parx);

  *arx = parx;
  return ret;
}

static int fast_actx_rxq_probe(struct dataplane_context *ctx, uint32_t id,
    uint16_t app_id)
{
  struct flextcp_pl_appctx *actx = &fp_state->appctx[ctx->id][app_id][id];
  struct flextcp_pl_arx *parx;
  uint32_t pos, i;

  if (actx->rx_avail > actx->rx_len / 2) {
    return -1;
  }

  pos = actx->rx_head + actx->rx_avail;
  if (pos >= actx->rx_len)
    pos -= actx->rx_len;

  i = 0;
  while (actx->rx_avail < actx->rx_len && i < 2 * BATCH_SIZE) {
    parx = dma_pointer(actx->rx_base + pos, sizeof(*parx));

    if (parx->type != 0) {
      break;
    }

    actx->rx_avail += sizeof(*parx);
    pos += sizeof(*parx);
    if (pos >= actx->rx_len)
      pos -= actx->rx_len;
    i++;

    MEM_BARRIER();
  }

  return 0;
}

void fast_actx_rxq_probe_active(struct dataplane_context *ctx)
{
  uint32_t cid, aid;
  struct polled_app *act_app;

  aid = ctx->act_head;
  do {
    act_app = &ctx->polled_apps[aid];
   
    cid = act_app->act_ctx_head;
    do {
    fast_actx_rxq_probe(ctx, cid, aid);
    cid = act_app->ctxs[cid].next;
    } while(cid != act_app->act_ctx_head); 
    
    aid = ctx->polled_apps[aid].next;
  } while (aid != ctx->act_head);
}

void fast_actx_rxq_probe_all(struct dataplane_context *ctx)
{
  unsigned int n;
  uint32_t aid;
  for (aid = 0; aid < FLEXNIC_PL_APPST_NUM; aid++)
  {
    for (n = 0; n < FLEXNIC_PL_APPCTX_NUM; n++) 
    { 
      fast_actx_rxq_probe(ctx, n, aid);
    }
  }
}

/*****************************************************************************/
/* Manages active app and contexts rings */

void remove_ctxs_from_active(struct dataplane_context *ctx, 
    struct polled_context *ctxs[BATCH_SIZE], int n)
{
  int i;
  uint32_t aid, cid;
  struct polled_app *p_app;

  for (i = 0; i < n; i++)
  {
    cid = ctxs[i]->id;
    aid = ctxs[i]->aid;
    p_app = &ctx->polled_apps[aid];
    remove_ctx_from_active(p_app, &p_app->ctxs[cid]);

    if (p_app->act_ctx_head == IDXLIST_INVAL)
    {
      remove_app_from_active(ctx, p_app);
    }

 
         }
}

void enqueue_ctx_to_active(struct polled_app *act_app, uint32_t cid) 
{
  // printf("enqueue_ctx_to_active (before): aid=%d, cid=%d, head=%d, tail=%d\n", act_app->id, cid, act_app->act_ctx_head, act_app->act_ctx_tail);
  if (act_app->act_ctx_tail == IDXLIST_INVAL)
  {
    act_app->act_ctx_tail = act_app->act_ctx_head = cid;
    act_app->ctxs[cid].prev = act_app->act_ctx_tail;
    act_app->ctxs[cid].next = act_app->act_ctx_head;
    act_app->ctxs[cid].flags |= FLAG_ACTIVE;
    // printf("enqueue_ctx_to_active (after): aid=%d, cid=%d, head=%d, tail=%d\n", act_app->id, cid, act_app->act_ctx_head, act_app->act_ctx_tail);
    return;
  }

  act_app->ctxs[act_app->act_ctx_tail].next = cid;
  act_app->ctxs[act_app->act_ctx_head].prev = cid;
  act_app->ctxs[cid].prev = act_app->act_ctx_tail;
  act_app->ctxs[cid].next = act_app->act_ctx_head;
  act_app->ctxs[cid].flags |= FLAG_ACTIVE;
  act_app->act_ctx_tail = cid;
  // printf("enqueue_ctx_to_active (after): aid=%d, cid=%d, head=%d, tail=%d\n", act_app->id, cid, act_app->act_ctx_head, act_app->act_ctx_tail);
}

void remove_ctx_from_active(struct polled_app *act_app, 
    struct polled_context *act_ctx)
{
  // printf("remove_ctx_from_active (before): aid=%d, cid=%d, head=%d, tail=%d\n", act_app->id, act_ctx->id, act_app->act_ctx_head, act_app->act_ctx_tail);
  if (act_app->act_ctx_tail == act_app->act_ctx_head)
  {
    act_app->act_ctx_head = act_app->act_ctx_tail = IDXLIST_INVAL;
    act_ctx->next = IDXLIST_INVAL;
    act_ctx->prev = IDXLIST_INVAL;
    act_ctx->flags &= ~FLAG_ACTIVE;
    act_ctx->null_rounds = 0;
    // printf("remove_ctx_from_active (after): aid=%d, cid=%d, head=%d, tail=%d\n", act_app->id, act_ctx->id, act_app->act_ctx_head, act_app->act_ctx_tail);
    return;
  }
  
  /* element is tail */
  if (act_ctx->next == act_app->act_ctx_head)
  {
    act_app->act_ctx_tail = act_ctx->prev;
  } else if (act_ctx->prev == act_app->act_ctx_tail) /* element is head */
  {
    act_app->act_ctx_head = act_ctx->next;
  }
  
  act_app->ctxs[act_ctx->prev].next = act_ctx->next;
  act_app->ctxs[act_ctx->next].prev = act_ctx->prev;
  act_ctx->next = IDXLIST_INVAL;
  act_ctx->prev = IDXLIST_INVAL;
  act_ctx->flags &= ~FLAG_ACTIVE;
  act_ctx->null_rounds = 0;
  // printf("remove_ctx_from_active (after): aid=%d, cid=%d, head=%d, tail=%d\n", act_app->id, act_ctx->id, act_app->act_ctx_head, act_app->act_ctx_tail);
}

void enqueue_app_to_active(struct dataplane_context *ctx, uint16_t aid)
{
  // printf("enqueue_app_to_active (before): aid=%d, head=%d, tail=%d\n", aid, ctx->act_head, ctx->act_tail);
  if (ctx->act_tail == IDXLIST_INVAL)
  {
    ctx->act_tail = ctx->act_head = aid;
    ctx->polled_apps[aid].prev = ctx->act_tail;
    ctx->polled_apps[aid].next = ctx->act_head;
    ctx->polled_apps[aid].flags |= FLAG_ACTIVE;
    // printf("enqueue_app_to_active (after): aid=%d, head=%d, tail=%d\n", aid, ctx->act_head, ctx->act_tail);
    return;
  }

  ctx->polled_apps[ctx->act_tail].next = aid;
  ctx->polled_apps[ctx->act_head].prev = aid;
  ctx->polled_apps[aid].prev = ctx->act_tail;
  ctx->polled_apps[aid].next = ctx->act_head;
  ctx->polled_apps[aid].flags |= FLAG_ACTIVE;
  ctx->act_tail = aid;
  // printf("enqueue_app_to_active (after): aid=%d, head=%d, tail=%d\n", aid, ctx->act_head, ctx->act_tail);
 }

void remove_app_from_active(struct dataplane_context *ctx, 
    struct polled_app *act_app)
{
  // printf("remove_app_from_active (before): aid=%d, head=%d, tail=%d\n", act_app->id, ctx->act_head, ctx->act_tail);
  /* one app in ring */
  if (ctx->act_tail == ctx->act_head)
  {
    ctx->act_head = ctx->act_tail = IDXLIST_INVAL;
    act_app->next = IDXLIST_INVAL;
    act_app->prev = IDXLIST_INVAL;
    act_app->flags &= ~FLAG_ACTIVE;
    // printf("remove_app_from_active (after): aid=%d, head=%d, tail=%d\n", act_app->id, ctx->act_head, ctx->act_tail);
    return;
  }
  
  /* element is tail */
  if (act_app->next == ctx->act_head)
  {
    ctx->act_tail = act_app->prev;
  } else if (act_app->prev == ctx->act_tail) /* element is head */
  {
    ctx->act_head = act_app->next;
  }
  
  ctx->polled_apps[act_app->prev].next = act_app->next;
  ctx->polled_apps[act_app->next].prev = act_app->prev;
  act_app->next = IDXLIST_INVAL;
  act_app->prev = IDXLIST_INVAL;
  act_app->flags &= ~FLAG_ACTIVE;
  // printf("remove_app_from_active (after): aid=%d, head=%d, tail=%d\n", act_app->id, ctx->act_head, ctx->act_tail);
}
