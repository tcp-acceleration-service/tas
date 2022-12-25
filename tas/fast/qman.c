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

// TODO: Add skiplist to app queues

/**
 * Complete queue manager implementation
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include <utils.h>
#include <utils_sync.h>

#include "internal.h"
#include "../slow/internal.h"


#define dprintf(...) do { } while (0)

#define FLAG_INSKIPLIST 1
#define FLAG_INNOLIMITL 2

/** Skiplist: bits per level */
#define SKIPLIST_BITS 3

#define RNG_SEED 0x12345678
#define TIMESTAMP_BITS 32
#define TIMESTAMP_MASK 0xFFFFFFFF

#define QUANTA BATCH_SIZE * TCP_MSS

/** Queue container for a virtual machine */
struct vm_qman {
  /** VM queue */
  struct vm_queue *queues;
  /** Idx of head of queue */
  uint32_t head_idx;
  /** Idx of tail of queue */
  uint32_t tail_idx;
};

/** Queue container for a flow **/
struct flow_qman {
  /** Flow queue */ 
  struct flow_queue *queues;
  /** Idx of heads of each level in the skiplist */
  uint32_t head_idx[QMAN_SKIPLIST_LEVELS];
  /** Idx of head of no limit queue */
  uint32_t nolimit_head_idx;
  /** Idx of tail of no limit queue */
  uint32_t nolimit_tail_idx;
  /** Whether to poll nolimit queue first */
  bool nolimit_first;
};

/** Queue state for a virtual machine */
struct vm_queue {
  /** Next pointer */
  uint32_t next_idx;
  /** Pointer to container with flows for this VM */
  struct flow_qman *fqman;
  /** Number of entries for this VM */
  uint32_t avail;
  /** Flags: FLAG_INNOLIMITL */
  uint16_t flags;
  /* Deficit counter */
  uint16_t dc;
  /* Bytes sent in this round for this VM. Reset every round. */
  uint16_t bytes;
};

/** Queue state for flow */
struct flow_queue {
  /** Next pointers for levels in skip list */
  uint32_t next_idxs[QMAN_SKIPLIST_LEVELS];
  /** Time stamp */
  uint32_t next_ts;
  /** Assigned Rate */
  uint32_t rate;
  /** Number of entries in queue */
  uint32_t avail;
  /** Maximum chunk size when de-queueing */
  uint16_t max_chunk;
  /** Flags: FLAG_INSKIPLIST, FLAG_INNOLIMITL */
  uint16_t flags;
} __attribute__((packed));
STATIC_ASSERT((sizeof(struct flow_queue) == 32), queue_size);

/** General qman functions */
static inline int64_t rel_time(uint32_t cur_ts, uint32_t ts_in);
static inline uint32_t sum_bytes(uint16_t *q_bytes, unsigned start, unsigned end);
static inline uint32_t timestamp(void);
static inline int timestamp_lessthaneq(struct qman_thread *t, uint32_t a,
    uint32_t b);

/** Qman functions for VM */
static inline int vmcont_init(struct qman_thread *t);
static inline int vm_qman_poll(struct qman_thread *t, 
    struct vm_qman *vqman, struct vm_budget *budgets,
    unsigned num, unsigned *vm_id, unsigned *q_ids, uint16_t *q_bytes);
static inline int vm_qman_set(struct qman_thread *t, uint32_t vm_id, uint32_t flow_id,
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
static inline void vm_queue_fire(struct vm_qman *vqman, struct vm_queue *q,
    uint32_t idx, uint16_t *q_bytes, unsigned start, unsigned end);
/** Actually update queue state for app queue */
static inline void vm_set_impl(struct vm_qman *vqman, uint32_t v_idx,
    uint32_t f_idx, uint32_t avail, uint8_t flags);
static inline void vm_queue_activate(struct vm_qman *vqman,
    struct vm_queue *q, uint32_t idx);

/** Qman management functions for flows */
static inline int flowcont_init(struct vm_queue *vq);
static inline int flow_qman_poll(struct qman_thread *t, struct vm_queue *vqueue,
    struct flow_qman *fqman, unsigned num, unsigned *q_ids, uint16_t *q_bytes);
int flow_qman_set(struct qman_thread *t, struct flow_qman *fqman, uint32_t flow_id,
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
/** Actually update queue state for flow queue: must run on queue's home core */
static inline void flow_set_impl(struct qman_thread *t, struct flow_qman *fqman, 
    uint32_t id, uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
/** Add queue to the flow no limit list */
static inline void flow_queue_activate_nolimit(struct flow_qman *fqman,
    struct flow_queue *q, uint32_t idx);
static inline unsigned flow_poll_nolimit(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman, 
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes);
/** Add queue to the flow skip list list */
static inline void flow_queue_activate_skiplist(struct qman_thread *t,
    struct flow_qman *fqman, struct flow_queue *q, uint32_t idx);
static inline unsigned flow_poll_skiplist(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman,
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes);
static inline uint8_t flow_queue_level(struct qman_thread *t, 
    struct flow_qman *fqman);
static inline void flow_queue_fire(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman,
    struct flow_queue *q, uint32_t idx, unsigned *q_id, uint16_t *q_bytes);
static inline void flow_queue_activate(struct qman_thread *t, struct flow_qman *fqman, 
    struct flow_queue *q, uint32_t idx);
static inline uint32_t flow_queue_new_ts(struct qman_thread *t, struct flow_queue *q,
    uint32_t bytes);


/*****************************************************************************/
/* Top level queue manager */

int qman_thread_init(struct dataplane_context *ctx)
{
  struct qman_thread *t = &ctx->qman;

  if (vmcont_init(t) != 0)
  {
    fprintf(stderr, "qman_thread_init: app_cont init failed\n");
    return -1;
  }

  utils_rng_init(&t->rng, RNG_SEED * ctx->id + ctx->id);
  t->ts_virtual = 0;
  t->ts_real = timestamp();

  return 0;
}

int qman_poll(struct dataplane_context *ctx, unsigned num, unsigned *vm_ids,
              unsigned *q_ids, uint16_t *q_bytes)
{
  int ret;
  struct qman_thread *t = &ctx->qman;
  struct vm_budget *budgets = ctx->budgets;
  struct vm_qman *vqman = t->vqman;

  ret = vm_qman_poll(t, vqman, budgets, num, vm_ids, q_ids, q_bytes);
  return ret;
}

int qman_set(struct qman_thread *t, uint32_t vm_id, uint32_t flow_id, uint32_t rate,
             uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  int ret;
  ret = vm_qman_set(t, vm_id, flow_id, rate, avail, max_chunk, flags);

  return ret;
}

uint32_t qman_next_ts(struct qman_thread *t, uint32_t cur_ts)
{
  struct vm_queue *vq;
  struct flow_qman *fqman;
  uint32_t ts = timestamp();
  uint32_t ret_ts = t->ts_virtual + (ts - t->ts_real);
  struct vm_qman *vqman = t->vqman;

  if (vqman->head_idx == IDXLIST_INVAL)
  {
    return -1;
  }

  vq = &vqman->queues[vqman->head_idx];
  fqman = vq->fqman;

  if (fqman->nolimit_head_idx != IDXLIST_INVAL)
  {
    // Nolimit queue has work - immediate timeout
    fprintf(stderr, "QMan nolimit has work\n");
    return 0;
  }

  uint32_t idx = fqman->head_idx[0];
  if (idx != IDXLIST_INVAL)
  {
    struct flow_queue *q = &fqman->queues[idx];

    if (timestamp_lessthaneq(t, q->next_ts, ret_ts))
    {
      // Fired in the past - immediate timeout
      return 0;
    }
    else
    {
      // Timeout in the future - return difference
      return rel_time(ret_ts, q->next_ts) / 1000;
    }
  }

  // List empty - no timeout
  return -1;
}

uint32_t qman_timestamp(uint64_t cycles)
{
  static uint64_t freq = 0;

  if (freq == 0)
    freq = rte_get_tsc_hz();

  cycles *= 1000000ULL;
  cycles /= freq;
  return cycles;
}

uint32_t timestamp(void)
{
  static uint64_t freq = 0;
  uint64_t cycles = rte_get_tsc_cycles();

  if (freq == 0)
    freq = rte_get_tsc_hz();
  cycles *= 1000000000ULL;
  cycles /= freq;
  return cycles;
}

/** Relative timestamp, ignoring wrap-arounds */
static inline int64_t rel_time(uint32_t cur_ts, uint32_t ts_in)
{
  uint64_t ts = ts_in;
  const uint64_t middle = (1ULL << (TIMESTAMP_BITS - 1));
  uint64_t start, end;

  if (cur_ts < middle)
  {
    /* negative interval is split in half */
    start = (cur_ts - middle) & TIMESTAMP_MASK;
    end = (1ULL << TIMESTAMP_BITS);
    if (start <= ts && ts < end)
    {
      /* in first half of negative interval, smallest timestamps */
      return ts - start - middle;
    }
    else
    {
      /* in second half or in positive interval */
      return ts - cur_ts;
    }
  }
  else if (cur_ts == middle)
  {
    /* intervals not split */
    return ts - cur_ts;
  }
  else
  {
    /* higher interval is split */
    start = 0;
    end = ((cur_ts + middle) & TIMESTAMP_MASK) + 1;
    if (start <= cur_ts && ts < end)
    {
      /* in second half of positive interval, largest timestamps */
      return ts + ((1ULL << TIMESTAMP_BITS) - cur_ts);
    }
    else
    {
      /* in negative interval or first half of positive interval */
      return ts - cur_ts;
    }
  }
}

int timestamp_lessthaneq(struct qman_thread *t, uint32_t a,
                         uint32_t b)
{
  return rel_time(t->ts_virtual, a) <= rel_time(t->ts_virtual, b);
}

/*****************************************************************************/

/*****************************************************************************/
/* Manages vm queues */

int vmcont_init(struct qman_thread *t)
{
  int ret;
  unsigned i;
  struct vm_queue *vq;
  t->vqman = malloc(sizeof(struct vm_qman));
  struct vm_qman *vqman = t->vqman;

  vqman->queues = calloc(1, sizeof(*vqman->queues) * (FLEXNIC_PL_VMST_NUM - 1));
  if (vqman->queues == NULL)
  {
    fprintf(stderr, "vmcont_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < FLEXNIC_PL_VMST_NUM - 1; i++)
  {
    vq = &vqman->queues[i];
    vq->avail = 0;
    vq->dc = QUANTA;
    ret = flowcont_init(vq);

    if (ret != 0)
    {
      return -1;
    }
  }

  vqman->head_idx = vqman->tail_idx = IDXLIST_INVAL;
  return 0;
}

static inline int vm_qman_poll(struct qman_thread *t, struct vm_qman *vqman, 
    struct vm_budget *budgets, unsigned num, 
    unsigned *vm_ids, unsigned *q_ids, uint16_t *q_bytes)
{
  int i, cnt, x;
  uint32_t idx;
  uint64_t s_cycs, e_cycs;

  for (cnt = 0; cnt < num && vqman->head_idx != IDXLIST_INVAL;)
  {
    s_cycs = util_rdtsc();
    idx = vqman->head_idx;
    struct vm_queue *vq = &vqman->queues[idx];
    struct flow_qman *fqman = vq->fqman;

    vqman->head_idx = vq->next_idx;
    if (vq->next_idx == IDXLIST_INVAL)
    {
      vqman->tail_idx = IDXLIST_INVAL;
    }

    vq->flags &= ~FLAG_INNOLIMITL;

    x = flow_qman_poll(t, vq, fqman, num - cnt, q_ids + cnt, q_bytes + cnt);

    cnt += x;

    // Update vm_id list
    for (i = cnt - x; i < cnt; i++)
    {
      vm_ids[i] = idx;
    }

    if (vq->avail > 0)
    {
      vm_queue_fire(vqman, vq, idx, q_bytes, cnt - x, cnt);
    }

    vq->dc += QUANTA;  
    e_cycs = util_rdtsc();

    util_spin_lock(&budgets[idx].lock);
    budgets[idx].cycles += e_cycs - s_cycs;
    util_spin_unlock(&budgets[idx].lock);

  }


  return cnt;
}

static inline int vm_qman_set(struct qman_thread *t, uint32_t vm_id, uint32_t flow_id, 
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  int ret;
  struct vm_qman *vqman = t->vqman;
  struct vm_queue *vq = &vqman->queues[vm_id];
  struct flow_qman *fqman = vq->fqman;

  if (vm_id >= FLEXNIC_PL_VMST_NUM) 
  {
    fprintf(stderr, "vm_qman_set: invalid vm id: %u >= %u\n", vm_id,
        FLEXNIC_PL_VMST_NUM);
    return -1;
  }

  vm_set_impl(vqman, vm_id, flow_id, avail, flags);
  ret = flow_qman_set(t, fqman, flow_id, rate, avail, max_chunk, flags);

  return ret;
}

static inline void vm_queue_fire(struct vm_qman *vqman, struct vm_queue *q,
    uint32_t idx, uint16_t *q_bytes, unsigned start, unsigned end)
{
  uint32_t bytes;
  assert(q->avail > 0);

  bytes = sum_bytes(q_bytes, start, end);
  q->avail -= bytes;

  if (q->avail > 0) {
    vm_queue_activate(vqman, q, idx);
  }

}

static inline void vm_set_impl(struct vm_qman *vqman, uint32_t v_idx,
    uint32_t f_idx, uint32_t avail, uint8_t flags)
{
  struct vm_queue *vq = &vqman->queues[v_idx];
  struct flow_qman *fqman = vq->fqman;
  struct flow_queue *fq = &fqman->queues[f_idx];

  int new_avail = 0;

  if ((flags & QMAN_SET_AVAIL) != 0)
  {
    new_avail = 1;
    int prev_avail = fq->avail;
    vq->avail -= prev_avail;
    vq->avail += avail;
  }
  else if ((flags & QMAN_ADD_AVAIL) != 0)
  {
    vq->avail += avail;
    new_avail = 1;
  }

  if (new_avail && vq->avail > 0 && ((vq->flags & (FLAG_INNOLIMITL)) == 0)) 
  {
    vm_queue_activate(vqman, vq, v_idx);
  }

}

static inline void vm_queue_activate(struct vm_qman *vqman,
    struct vm_queue *q, uint32_t idx)
{
  struct vm_queue *q_tail;

  assert((q->flags & FLAG_INNOLIMITL) == 0);

  q->flags |= FLAG_INNOLIMITL;
  q->next_idx = IDXLIST_INVAL;
  if (vqman->tail_idx == IDXLIST_INVAL)
  {
    vqman->head_idx = vqman->tail_idx = idx;
    return;
  }

  q_tail = &vqman->queues[vqman->tail_idx];
  q_tail->next_idx = idx;
  vqman->tail_idx = idx;
}

static inline uint32_t sum_bytes(uint16_t *q_bytes, unsigned start, unsigned end)
{
  int i;
  uint32_t bytes = 0;
  for (i = start; i < end; i++)
  {
    bytes += q_bytes[i];
  }

  return bytes;
}

/*****************************************************************************/

/*****************************************************************************/
/* Manages flow queues */

int flowcont_init(struct vm_queue *vq) 
{
  unsigned i;
  struct flow_qman *fqman;

  vq->fqman = malloc(sizeof(struct flow_qman));
  fqman = vq->fqman;

  fqman->queues = calloc(1, sizeof(*fqman->queues) * FLEXNIC_NUM_QMFLOWQUEUES);
  if (fqman->queues == NULL)
  {
    fprintf(stderr, "flowcont_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < QMAN_SKIPLIST_LEVELS; i++) 
  {
    fqman->head_idx[i] = IDXLIST_INVAL;
  }
  fqman->nolimit_head_idx = fqman->nolimit_tail_idx = IDXLIST_INVAL;

  return 0;
}

static inline int flow_qman_poll(struct qman_thread *t, struct vm_queue *vqueue, 
    struct flow_qman *fqman, unsigned num, unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned x, y;
  uint32_t ts = timestamp();

  /* poll nolimit list and skiplist alternating the order between */
  if (fqman->nolimit_first) {
    x = flow_poll_nolimit(t, vqueue, fqman, ts, num, q_ids, q_bytes);
    y = flow_poll_skiplist(t, vqueue, fqman, ts, num - x, q_ids + x, q_bytes + x);
  } else {
    x = flow_poll_skiplist(t, vqueue, fqman, ts, num, q_ids, q_bytes);
    y = flow_poll_nolimit(t, vqueue, fqman, ts, num - x, q_ids + x, q_bytes + x);
  }
  fqman->nolimit_first = !fqman->nolimit_first;

  return x + y;
}

int flow_qman_set(struct qman_thread *t, struct flow_qman *fqman, uint32_t id, 
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
#ifdef FLEXNIC_TRACE_QMAN
  struct flexnic_trace_entry_qman_set evt = {
      .id = id, .rate = rate, .avail = avail, .max_chunk = max_chunk,
      .flags = flags,
    };
  trace_event(FLEXNIC_TRACE_EV_QMSET, sizeof(evt), &evt);
#endif

  dprintf("flow_qman_set: id=%u rate=%u avail=%u max_chunk=%u qidx=%u tid=%u\n",
      id, rate, avail, max_chunk, qidx, tid);

  if (id >= FLEXNIC_NUM_QMFLOWQUEUES) {
    fprintf(stderr, "flow_qman_set: invalid queue id: %u >= %u\n", id,
        FLEXNIC_NUM_QMFLOWQUEUES);
    return -1;
  }

  flow_set_impl(t, fqman, id, rate, avail, max_chunk, flags);

  return 0;
}

/** Actually update queue state: must run on queue's home core */
static void inline flow_set_impl(struct qman_thread *t, struct flow_qman *fqman, 
    uint32_t idx, uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  struct flow_queue *q = &fqman->queues[idx];
  int new_avail = 0;

  if ((flags & QMAN_SET_RATE) != 0) {
    q->rate = rate;
  }

  if ((flags & QMAN_SET_MAXCHUNK) != 0) {
    q->max_chunk = max_chunk;
  }

  if ((flags & QMAN_SET_AVAIL) != 0) {
    q->avail = avail;
    new_avail = 1;
  } else if ((flags & QMAN_ADD_AVAIL) != 0) {
    q->avail += avail;
    new_avail = 1;
  }

  dprintf("flow_set_impl: t=%p q=%p idx=%u avail=%u rate=%u qflags=%x flags=%x\n", t, q, idx, q->avail, q->rate, q->flags, flags);

  if (new_avail && q->avail > 0
      && ((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0)) {
    flow_queue_activate(t, fqman, q, idx);
  }
}

/** Add queue to the no limit list for flows */
static inline void flow_queue_activate_nolimit(struct flow_qman *fqman,
    struct flow_queue *q, uint32_t idx)
{
  struct flow_queue *q_tail;

  assert((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0);

  dprintf("flow_queue_activate_nolimit: t=%p q=%p avail=%u rate=%u flags=%x\n", t, q, q->avail, q->rate, q->flags);

  q->flags |= FLAG_INNOLIMITL;
  q->next_idxs[0] = IDXLIST_INVAL;
  if (fqman->nolimit_tail_idx == IDXLIST_INVAL) 
  {
    fqman->nolimit_head_idx = fqman->nolimit_tail_idx = idx;
    return;
  }

  q_tail = &fqman->queues[fqman->nolimit_tail_idx];
  q_tail->next_idxs[0] = idx;
  fqman->nolimit_tail_idx = idx;
}

/** Poll no-limit queues for flows */
static inline unsigned flow_poll_nolimit(struct qman_thread *t, struct vm_queue *vqueue,
    struct flow_qman *fqman, uint32_t cur_ts, unsigned num,
    unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned cnt;
  struct flow_queue *q;
  uint32_t idx;

  for (cnt = 0; cnt < num && fqman->nolimit_head_idx != IDXLIST_INVAL
      && vqueue->dc > 0;) {
    idx = fqman->nolimit_head_idx;
    q = fqman->queues + idx;

    fqman->nolimit_head_idx = q->next_idxs[0];
    if (q->next_idxs[0] == IDXLIST_INVAL)
      fqman->nolimit_tail_idx = IDXLIST_INVAL;

    q->flags &= ~FLAG_INNOLIMITL;
    dprintf("flow_poll_nolimit: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n", t, q, idx, q->avail, q->rate, q->flags);
    if (q->avail > 0) {
      flow_queue_fire(t, vqueue, fqman, q, idx, q_ids + cnt, q_bytes + cnt);
      cnt++;
    }
  }

  return cnt;
}

/** Add queue to the flows skip list */
static inline void flow_queue_activate_skiplist(struct qman_thread *t, 
    struct flow_qman *fqman, struct flow_queue *q, uint32_t q_idx)
{
  uint8_t level;
  int8_t l;
  uint32_t preds[QMAN_SKIPLIST_LEVELS];
  uint32_t pred, idx, ts, max_ts;

  assert((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0);

  dprintf("flow_queue_activate_skiplist: t=%p q=%p idx=%u avail=%u rate=%u flags=%x ts_virt=%u next_ts=%u\n", t, q, q_idx, q->avail, q->rate, q->flags,
      t->ts_virtual, q->next_ts);

  /* make sure queue has a reasonable next_ts:
   *  - not in the past
   *  - not more than if it just sent max_chunk at the current rate
   */
  ts = q->next_ts;
  max_ts = flow_queue_new_ts(t, q, q->max_chunk);
  if (timestamp_lessthaneq(t, ts, t->ts_virtual)) {
    ts = q->next_ts = t->ts_virtual;
  } else if (!timestamp_lessthaneq(t, ts, max_ts)) {
    ts = q->next_ts = max_ts;
  }
  q->next_ts = ts;

  /* find predecessors at all levels top-down */
  pred = IDXLIST_INVAL;
  for (l = QMAN_SKIPLIST_LEVELS - 1; l >= 0; l--) {
    idx = (pred != IDXLIST_INVAL ? pred : fqman->head_idx[l]);
    while (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(t, fqman->queues[idx].next_ts, ts))
    {
      
      pred = idx;
      idx = fqman->queues[idx].next_idxs[l];
    }
    preds[l] = pred;
    dprintf("    pred[%u] = %d\n", l, pred);
  }

  /* determine level for this queue */
  level = flow_queue_level(t, fqman);
  dprintf("    level = %u\n", level);

  /* insert into skip-list */
  for (l = QMAN_SKIPLIST_LEVELS - 1; l >= 0; l--) {
    if (l > level) {
      q->next_idxs[l] = IDXLIST_INVAL;
    } else {
      idx = preds[l];
      if (idx != IDXLIST_INVAL) {
        q->next_idxs[l] = fqman->queues[idx].next_idxs[l];
        fqman->queues[idx].next_idxs[l] = q_idx;
      } else {
        q->next_idxs[l] = fqman->head_idx[l];
        fqman->head_idx[l] = q_idx;
      }
    }
  }

  q->flags |= FLAG_INSKIPLIST;
}

/** Poll skiplist queues for flows */
static inline unsigned flow_poll_skiplist(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman,
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned cnt;
  uint32_t idx, max_vts;
  int8_t l;
  struct flow_queue *q;

  /* maximum virtual time stamp that can be reached */
  max_vts = t->ts_virtual + (cur_ts - t->ts_real);

  for (cnt = 0; cnt < num;) {
    idx = fqman->head_idx[0];

    /* no more queues */
    if (idx == IDXLIST_INVAL) {
      t->ts_virtual = max_vts;
      break;
    }

    q = &fqman->queues[idx];

    /* beyond max_vts */
    dprintf("flow_poll_skiplist: next_ts=%u vts=%u rts=%u max_vts=%u cur_ts=%u\n",
        q->next_ts, t->ts_virtual, t->ts_real, max_vts, cur_ts);
    if (!timestamp_lessthaneq(t, q->next_ts, max_vts)) {
      t->ts_virtual = max_vts;
      break;
    }

    /* remove queue from skiplist */
    for (l = 0; l < QMAN_SKIPLIST_LEVELS && fqman->head_idx[l] == idx; l++) {
      fqman->head_idx[l] = q->next_idxs[l];
    }
    assert((q->flags & FLAG_INSKIPLIST) != 0);
    q->flags &= ~FLAG_INSKIPLIST;

    /* advance virtual timestamp */
    t->ts_virtual = q->next_ts;

    dprintf("flow_poll_skiplist: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n", t, q, idx, q->avail, q->rate, q->flags);

    if (q->avail > 0) {
      flow_queue_fire(t, vqueue, fqman, q, idx, q_ids + cnt, q_bytes + cnt);
      cnt++;
    }

  }

  /* if we reached the limit, update the virtual timestamp correctly */
  if (cnt == num) {
    idx = fqman->head_idx[0];
    if (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(t, fqman->queues[idx].next_ts, max_vts))
    {
      t->ts_virtual = fqman->queues[idx].next_ts;
    } else 
    {
      t->ts_virtual = max_vts;
    }
  }

  t->ts_real = cur_ts;
  return cnt;
}

static inline uint32_t flow_queue_new_ts(struct qman_thread *t, struct flow_queue *q,
    uint32_t bytes)
{
  return t->ts_virtual + ((uint64_t) bytes * 8 * 1000000) / q->rate;
}

/** Level for queue added to skiplist for flows*/
static inline uint8_t flow_queue_level(struct qman_thread *t, struct flow_qman *fqman)
{
  uint8_t x = (__builtin_ffs(utils_rng_gen32(&t->rng)) - 1) / SKIPLIST_BITS;
  return (x < QMAN_SKIPLIST_LEVELS ? x : QMAN_SKIPLIST_LEVELS - 1);
}

static inline void flow_queue_fire(struct qman_thread *t,
    struct vm_queue *vqueue, struct flow_qman *fqman, 
    struct flow_queue *q, uint32_t idx, unsigned *q_id, uint16_t *q_bytes)
{
  uint32_t bytes;

  assert(q->avail > 0);

  bytes = (q->avail <= q->max_chunk ? q->avail : q->max_chunk);
  bytes = (vqueue->dc < bytes ? vqueue->dc : bytes);
  q->avail -= bytes;

  dprintf("flow_queue_fire: t=%p q=%p idx=%u gidx=%u bytes=%u avail=%u rate=%u\n", t, q, idx, idx, bytes, q->avail, q->rate);
  if (q->rate > 0) 
  {
    q->next_ts = flow_queue_new_ts(t, q, bytes);
  }

  if (q->avail > 0) 
  {
    flow_queue_activate(t, fqman, q, idx);
  }

  vqueue->dc -= bytes;
  *q_bytes = bytes;
  *q_id = idx;

#ifdef FLEXNIC_TRACE_QMAN
  struct flexnic_trace_entry_qman_event evt = {
      .id = *q_id, .bytes = bytes,
    };
  trace_event(FLEXNIC_TRACE_EV_QMEVT, sizeof(evt), &evt);
#endif

  /* Return wether in the beginning the number of available bytes
     was smaller than the deficit counter this is used to determine
     wether we should stop scheduling packets for this VM in this round */
}

static inline void flow_queue_activate(struct qman_thread *t, struct flow_qman *fqman,
    struct flow_queue *q, uint32_t idx)
{
  if (q->rate == 0) {
    flow_queue_activate_nolimit(fqman, q, idx);
  } else {
    flow_queue_activate_skiplist(t, fqman, q, idx);
  }
}

/*****************************************************************************/

/*****************************************************************************/
/* Helper functions for unit tests */

void qman_free_vm_cont(struct dataplane_context *ctx)
{
  int i;
  struct vm_qman *vqman;
  struct vm_queue *vq;
  struct flow_qman *fqman;

  vqman = ctx->qman.vqman;

  for (i = 0; i < FLEXNIC_PL_VMST_NUM - 1; i++)
  {
    vq = &vqman->queues[i];
    fqman = vq->fqman;
    free(fqman->queues);
    free(fqman);
  }

  free(vqman->queues);
  free(vqman);
}

uint32_t qman_vm_get_avail(struct dataplane_context *ctx, uint32_t vm_id)
{
  uint32_t avail;
  struct vm_qman *vqman;
  struct vm_queue *vq;
  
  vqman = ctx->qman.vqman;
  vq = &vqman->queues[vm_id];
  avail = vq->avail;
  
  return avail;
}