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

#include <utils.h>
#include <utils_sync.h>
#include <tas_memif.h>

#include "internal.h"
#include "fastemu.h"

static void fast_appctx_poll_pf(struct dataplane_context *ctx, uint32_t id, 
    uint16_t vm_id);
static int fast_appctx_poll_fetch(struct dataplane_context *ctx, uint32_t actx_id,
    uint16_t vm_id, void **pqe);
static int fast_actx_rxq_probe(struct dataplane_context *ctx, uint32_t id,
    uint16_t vm_id);
    
void fast_appctx_poll_pf_active(struct dataplane_context *ctx, uint8_t *out_of_budget)
{
  uint32_t cid, vmid;
  struct polled_vm *act_vm;
  uint64_t s_cycs, e_cycs;

  vmid = ctx->act_head;
  do {

    if (ctx->budgets[vmid].cycles > 0)
    {
      s_cycs = util_rdtsc();
      act_vm = &ctx->polled_vms[vmid];
      cid = act_vm->act_ctx_head;
      do {
        fast_appctx_poll_pf(ctx, cid, vmid);
        cid = act_vm->ctxs[cid].next;
      } while(cid != act_vm->act_ctx_head);
      e_cycs = util_rdtsc();

      __sync_fetch_and_sub(&ctx->budgets[vmid].cycles, e_cycs - s_cycs);
    } else
    {
      out_of_budget[vmid] = 1;
    }

    vmid = ctx->polled_vms[vmid].next;
  } while (vmid != ctx->act_head);
} 

void fast_appctx_poll_pf_all(struct dataplane_context *ctx, uint8_t *out_of_budget)
{
  unsigned int i, j;
  uint32_t vmid, cid;
  uint64_t s_cycs, e_cycs;

  for  (i = 0; i < FLEXNIC_PL_VMST_NUM - 1; i++)
  {
    vmid = (ctx->poll_next_vm + i) % (FLEXNIC_PL_VMST_NUM - 1);
    
    if (ctx->budgets[vmid].cycles > 0)
    {
      s_cycs = util_rdtsc();
      for (j = 0; j < FLEXNIC_PL_APPCTX_NUM; j++) 
      {
        cid = (ctx->polled_vms[vmid].poll_next_ctx + j) % FLEXNIC_PL_APPCTX_NUM;
        fast_appctx_poll_pf(ctx, cid, vmid);
      }
      e_cycs = util_rdtsc();

      __sync_fetch_and_sub(&ctx->budgets[vmid].cycles, e_cycs - s_cycs);
    }
  }
}

static void fast_appctx_poll_pf(struct dataplane_context *ctx, uint32_t cid, 
    uint16_t vmid)
{
  struct flextcp_pl_appctx *actx = &fp_state->appctx[ctx->id][vmid][cid];
  rte_prefetch0(dma_pointer(actx->tx_base + actx->tx_head, 1, vmid));
}

int fast_appctx_poll_fetch_active(struct dataplane_context *ctx, uint16_t max,
    unsigned *total, int *n_rem, struct polled_context *rem_ctxs[BATCH_SIZE], 
    void *aqes[BATCH_SIZE], uint8_t *out_of_budget)
{
  int ret;
  unsigned i_b;
  uint16_t k = 0;
  uint32_t vmid, cid;
  struct polled_vm *act_vm;
  struct polled_context *act_ctx;
  uint64_t s_cycs, e_cycs;
  
  vmid = ctx->act_head;
  do {
    act_vm = &ctx->polled_vms[vmid];

    if (!out_of_budget[vmid])
    {
        cid = act_vm->act_ctx_head;
        do  {
          s_cycs = util_rdtsc();
          act_ctx = &act_vm->ctxs[cid];
          for (i_b = 0; i_b < BATCH_SIZE && k < max; i_b++) 
          {
            ret = fast_appctx_poll_fetch(ctx, cid, vmid, &aqes[k]);
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
        } while(cid != act_vm->act_ctx_head && k < max);

        act_vm->act_ctx_head = act_vm->ctxs[act_vm->act_ctx_head].next;
        act_vm->act_ctx_tail = act_vm->ctxs[act_vm->act_ctx_tail].next;
        e_cycs = util_rdtsc();
        __sync_fetch_and_sub(&ctx->budgets[vmid].cycles, e_cycs - s_cycs);
    }

    vmid = ctx->polled_vms[vmid].next; 
  } while (vmid != ctx->act_head && k < max);

  return k;
}

int fast_appctx_poll_fetch_all(struct dataplane_context *ctx, uint16_t max,
    unsigned *total, void *aqes[BATCH_SIZE], uint8_t *out_of_budget)
{
  int ret;
  unsigned i_v, i_c, i_b;
  uint16_t k = 0;
  uint32_t next_vm, next_ctx;
  struct polled_vm *p_vm;
  struct polled_context *p_ctx;
  uint64_t s_cycs, e_cycs;

  for (i_v = 0; i_v < FLEXNIC_PL_VMST_NUM - 1 && k < max; i_v++)
  {
    next_vm = ctx->poll_next_vm;
    p_vm = &ctx->polled_vms[next_vm];

    if (!out_of_budget[next_vm])
    {
      s_cycs = util_rdtsc();
      for (i_c = 0; i_c < FLEXNIC_PL_APPCTX_NUM && k < max; i_c++) 
      {
        next_ctx = p_vm->poll_next_ctx;
        p_ctx = &p_vm->ctxs[next_ctx];
        for (i_b = 0; i_b < BATCH_SIZE && k < max; i_b++) 
        {
          ret = fast_appctx_poll_fetch(ctx, next_ctx, next_vm, &aqes[k]);
          if (ret == 0) 
          {
            p_ctx->null_rounds = 0;
            
            /* Add vm to active list if it is not already in list */
            if ((p_vm->flags & FLAG_ACTIVE) == 0)
            {
              enqueue_vm_to_active(ctx, next_vm);
            }

            /* Add vm ctx to active list if it is not already in list */
            if ((p_ctx->flags & FLAG_ACTIVE) == 0)
            {
              enqueue_ctx_to_active(p_vm, next_ctx);
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
        p_vm->poll_next_ctx = (p_vm->poll_next_ctx + 1) % FLEXNIC_PL_APPCTX_NUM;
      }
      e_cycs = util_rdtsc();
      __sync_fetch_and_sub(&ctx->budgets[next_vm].cycles, e_cycs - s_cycs);
    }
    ctx->poll_next_vm = (ctx->poll_next_vm + 1) % (FLEXNIC_PL_VMST_NUM - 1);
  }

  return k;
}

static int fast_appctx_poll_fetch(struct dataplane_context *ctx, uint32_t actx_id,
    uint16_t vm_id, void **pqe)
{
  struct flextcp_pl_appctx *actx = &fp_state->appctx[ctx->id][vm_id][actx_id];
  struct flextcp_pl_atx *atx;
  uint8_t type;
  uint32_t flow_id  = -1;

  /* stop if context is not in use */
  if (actx->tx_len == 0)
    return -1;

  atx = dma_pointer(actx->tx_base + actx->tx_head, sizeof(*atx), vm_id);

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
    struct flextcp_pl_appctx *actx, uint16_t vmid)
{
  rte_prefetch0(dma_pointer(actx->rx_base + actx->rx_head,
        sizeof(struct flextcp_pl_arx), vmid));
}

int fast_actx_rxq_alloc(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_arx **arx, uint16_t vmid)
{
  struct flextcp_pl_arx *parx;
  uint32_t rxnhead;
  int ret = 0;

  if (actx->rx_avail == 0) {
    return -1;
  }

  MEM_BARRIER();
  parx = dma_pointer(actx->rx_base + actx->rx_head, sizeof(*parx), vmid);

  rxnhead = actx->rx_head + sizeof(*parx);
  if (rxnhead >= actx->rx_len) {
    rxnhead -= actx->rx_len;
  }
  actx->rx_head = rxnhead;
  actx->rx_avail -= sizeof(*parx);

  *arx = parx;
  return ret;
}

static int fast_actx_rxq_probe(struct dataplane_context *ctx, uint32_t cid,
    uint16_t vmid)
{
  struct flextcp_pl_appctx *actx = &fp_state->appctx[ctx->id][vmid][cid];
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
    parx = dma_pointer(actx->rx_base + pos, sizeof(*parx), vmid);

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

void fast_actx_rxq_probe_active(struct dataplane_context *ctx, uint8_t *out_of_budget)
{
  uint32_t cid, vmid;
  struct polled_vm *act_vm;
  uint64_t s_cycs, e_cycs;

  vmid = ctx->act_head;
  do {
    act_vm = &ctx->polled_vms[vmid];
   
    if (!out_of_budget[vmid])
    {
      s_cycs = util_rdtsc();
      cid = act_vm->act_ctx_head;
      do {
      fast_actx_rxq_probe(ctx, cid, vmid);
      cid = act_vm->ctxs[cid].next;
      } while(cid != act_vm->act_ctx_head); 
      e_cycs = util_rdtsc();
      __sync_fetch_and_sub(&ctx->budgets[vmid].cycles, e_cycs - s_cycs);
    }

    vmid = ctx->polled_vms[vmid].next;
  } while (vmid != ctx->act_head);
}

void fast_actx_rxq_probe_all(struct dataplane_context *ctx, uint8_t *out_of_budget)
{
  unsigned int n;
  uint32_t vmid;
  uint64_t s_cycs, e_cycs;

  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM - 1; vmid++)
  {
    if (!out_of_budget[vmid])
    {
      s_cycs = util_rdtsc();
      for (n = 0; n < FLEXNIC_PL_APPCTX_NUM; n++) 
      { 
        fast_actx_rxq_probe(ctx, n, vmid);
      }
      e_cycs = util_rdtsc();

      __sync_fetch_and_sub(&ctx->budgets[vmid].cycles, e_cycs - s_cycs);
    }
  }
}

/*****************************************************************************/
/* Manages active vms and contexts rings */

void remove_ctxs_from_active(struct dataplane_context *ctx, 
    struct polled_context *ctxs[BATCH_SIZE], int n)
{
  int i;
  uint32_t vmid, cid;
  struct polled_vm *p_vm;

  for (i = 0; i < n; i++)
  {
    cid = ctxs[i]->id;
    vmid = ctxs[i]->vmid;
    p_vm = &ctx->polled_vms[vmid];
    remove_ctx_from_active(p_vm, &p_vm->ctxs[cid]);

    if (p_vm->act_ctx_head == IDXLIST_INVAL)
    {
      remove_vm_from_active(ctx, p_vm);
    }
  }
}

void enqueue_ctx_to_active(struct polled_vm *act_vm, uint32_t cid) 
{
  if (act_vm->act_ctx_tail == IDXLIST_INVAL)
  {
    act_vm->act_ctx_tail = act_vm->act_ctx_head = cid;
    act_vm->ctxs[cid].prev = act_vm->act_ctx_tail;
    act_vm->ctxs[cid].next = act_vm->act_ctx_head;
    act_vm->ctxs[cid].flags |= FLAG_ACTIVE;
    return;
  }

  act_vm->ctxs[act_vm->act_ctx_tail].next = cid;
  act_vm->ctxs[act_vm->act_ctx_head].prev = cid;
  act_vm->ctxs[cid].prev = act_vm->act_ctx_tail;
  act_vm->ctxs[cid].next = act_vm->act_ctx_head;
  act_vm->ctxs[cid].flags |= FLAG_ACTIVE;
  act_vm->act_ctx_tail = cid;
}

void remove_ctx_from_active(struct polled_vm *act_vm, 
    struct polled_context *act_ctx)
{
  if (act_vm->act_ctx_tail == act_vm->act_ctx_head)
  {
    act_vm->act_ctx_head = act_vm->act_ctx_tail = IDXLIST_INVAL;
    act_ctx->next = IDXLIST_INVAL;
    act_ctx->prev = IDXLIST_INVAL;
    act_ctx->flags &= ~FLAG_ACTIVE;
    act_ctx->null_rounds = 0;
    return;
  }
  
  /* element is tail */
  if (act_ctx->next == act_vm->act_ctx_head)
  {
    act_vm->act_ctx_tail = act_ctx->prev;
  } else if (act_ctx->prev == act_vm->act_ctx_tail) /* element is head */
  {
    act_vm->act_ctx_head = act_ctx->next;
  }
  
  act_vm->ctxs[act_ctx->prev].next = act_ctx->next;
  act_vm->ctxs[act_ctx->next].prev = act_ctx->prev;
  act_ctx->next = IDXLIST_INVAL;
  act_ctx->prev = IDXLIST_INVAL;
  act_ctx->flags &= ~FLAG_ACTIVE;
  act_ctx->null_rounds = 0;
}

void enqueue_vm_to_active(struct dataplane_context *ctx, uint16_t vmid)
{
  if (ctx->act_tail == IDXLIST_INVAL)
  {
    ctx->act_tail = ctx->act_head = vmid;
    ctx->polled_vms[vmid].prev = ctx->act_tail;
    ctx->polled_vms[vmid].next = ctx->act_head;
    ctx->polled_vms[vmid].flags |= FLAG_ACTIVE;
    return;
  }

  ctx->polled_vms[ctx->act_tail].next = vmid;
  ctx->polled_vms[ctx->act_head].prev = vmid;
  ctx->polled_vms[vmid].prev = ctx->act_tail;
  ctx->polled_vms[vmid].next = ctx->act_head;
  ctx->polled_vms[vmid].flags |= FLAG_ACTIVE;
  ctx->act_tail = vmid;
 }

void remove_vm_from_active(struct dataplane_context *ctx, 
    struct polled_vm *act_vm)
{
  /* one vm in ring */
  if (ctx->act_tail == ctx->act_head)
  {
    ctx->act_head = ctx->act_tail = IDXLIST_INVAL;
    act_vm->next = IDXLIST_INVAL;
    act_vm->prev = IDXLIST_INVAL;
    act_vm->flags &= ~FLAG_ACTIVE;
    return;
  }
  
  /* element is tail */
  if (act_vm->next == ctx->act_head)
  {
    ctx->act_tail = act_vm->prev;
  } else if (act_vm->prev == ctx->act_tail) /* element is head */
  {
    ctx->act_head = act_vm->next;
  }
  
  ctx->polled_vms[act_vm->prev].next = act_vm->next;
  ctx->polled_vms[act_vm->next].prev = act_vm->prev;
  act_vm->next = IDXLIST_INVAL;
  act_vm->prev = IDXLIST_INVAL;
  act_vm->flags &= ~FLAG_ACTIVE;
}
