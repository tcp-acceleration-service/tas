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

/**
 * Full queue manager implementation with rate-limits
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

#include "internal.h"


/** Actually update queue state: must run on queue's home core */
static inline void flow_set_impl(struct qman_thread *t, struct flow_cont *fc, 
    uint32_t id, uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);

/** Add queue to the no limit list */
static inline void flow_queue_activate_nolimit(struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx);
static inline unsigned flow_poll_nolimit(struct qman_thread *t, struct flow_cont *fc, 
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes);

/** Add queue to the skip list list */
static inline void flow_queue_activate_skiplist(struct qman_thread *t,
    struct flow_cont *fc, struct flow_queue *q, uint32_t idx);
static inline unsigned flow_poll_skiplist(struct qman_thread *t, struct flow_cont *fc,
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes);
static inline uint8_t flow_queue_level(struct qman_thread *t, 
    struct flow_cont *fc);

static inline void flow_queue_fire(struct qman_thread *t, struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx, unsigned *q_id, uint16_t *q_bytes);
static inline void flow_queue_activate(struct qman_thread *t, struct flow_cont *fc, 
    struct flow_queue *q, uint32_t idx);
static inline uint32_t flow_queue_new_ts(struct qman_thread *t, struct flow_queue *q,
    uint32_t bytes);


int flowcont_init(struct app_queue *aq) 
{
  unsigned i;
  struct flow_cont *fc;

  aq->f_cont = malloc(sizeof(struct flow_cont));
  fc = aq->f_cont;

  fc->queues = calloc(1, sizeof(*fc->queues) * FLEXNIC_NUM_QMFLOWQUEUES);
  if (fc->queues == NULL)
  {
    fprintf(stderr, "flowcont_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < QMAN_SKIPLIST_LEVELS; i++) 
  {
    fc->head_idx[i] = IDXLIST_INVAL;
  }
  fc->nolimit_head_idx = fc->nolimit_tail_idx = IDXLIST_INVAL;

  return 0;
}

int flow_qman_poll(struct qman_thread *t, struct flow_cont *fc, unsigned num, 
    unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned x, y;
  uint32_t ts = timestamp();

  /* poll nolimit list and skiplist alternating the order between */
  if (fc->nolimit_first) {
    x = flow_poll_nolimit(t, fc, ts, num, q_ids, q_bytes);
    y = flow_poll_skiplist(t, fc, ts, num - x, q_ids + x, q_bytes + x);
  } else {
    x = flow_poll_skiplist(t, fc, ts, num, q_ids, q_bytes);
    y = flow_poll_nolimit(t, fc, ts, num - x, q_ids + x, q_bytes + x);
  }
  fc->nolimit_first = !fc->nolimit_first;

  return x + y;
}

int flow_qman_set(struct qman_thread *t, struct flow_cont *fc, uint32_t id, 
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

  flow_set_impl(t, fc, id, rate, avail, max_chunk, flags);

  return 0;
}

/** Actually update queue state: must run on queue's home core */
static void inline flow_set_impl(struct qman_thread *t, struct flow_cont *fc, 
    uint32_t idx, uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  struct flow_queue *q = &fc->queues[idx];
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
    flow_queue_activate(t, fc, q, idx);
  }
}

/*****************************************************************************/
/* Managing no-limit queues */

/** Add queue to the no limit list */
static inline void flow_queue_activate_nolimit(struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx)
{
  struct flow_queue *q_tail;

  assert((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0);

  dprintf("flow_queue_activate_nolimit: t=%p q=%p avail=%u rate=%u flags=%x\n", t, q, q->avail, q->rate, q->flags);

  q->flags |= FLAG_INNOLIMITL;
  q->next_idxs[0] = IDXLIST_INVAL;
  if (fc->nolimit_tail_idx == IDXLIST_INVAL) {
    fc->nolimit_head_idx = fc->nolimit_tail_idx = idx;
    return;
  }

  q_tail = &fc->queues[fc->nolimit_tail_idx];
  q_tail->next_idxs[0] = idx;
  fc->nolimit_tail_idx = idx;
}

/** Poll no-limit queues */
static inline unsigned flow_poll_nolimit(struct qman_thread *t, struct flow_cont *fc,
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned cnt;
  struct flow_queue *q;
  uint32_t idx;

  for (cnt = 0; cnt < num && fc->nolimit_head_idx != IDXLIST_INVAL;) {
    idx = fc->nolimit_head_idx;
    q = fc->queues + idx;

    fc->nolimit_head_idx = q->next_idxs[0];
    if (q->next_idxs[0] == IDXLIST_INVAL)
      fc->nolimit_tail_idx = IDXLIST_INVAL;

    q->flags &= ~FLAG_INNOLIMITL;
    dprintf("flow_poll_nolimit: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n", t, q, idx, q->avail, q->rate, q->flags);
    if (q->avail > 0) {
      flow_queue_fire(t, fc, q, idx, q_ids + cnt, q_bytes + cnt);
      cnt++;
    }
  }

  return cnt;
}

/** Add queue to the skip list list */
static inline void flow_queue_activate_skiplist(struct qman_thread *t, 
    struct flow_cont *fc, struct flow_queue *q, uint32_t q_idx)
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
    idx = (pred != IDXLIST_INVAL ? pred : fc->head_idx[l]);
    while (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(t, fc->queues[idx].next_ts, ts))
    {
      
      pred = idx;
      idx = fc->queues[idx].next_idxs[l];
    }
    preds[l] = pred;
    dprintf("    pred[%u] = %d\n", l, pred);
  }

  /* determine level for this queue */
  level = flow_queue_level(t, fc);
  dprintf("    level = %u\n", level);

  /* insert into skip-list */
  for (l = QMAN_SKIPLIST_LEVELS - 1; l >= 0; l--) {
    if (l > level) {
      q->next_idxs[l] = IDXLIST_INVAL;
    } else {
      idx = preds[l];
      if (idx != IDXLIST_INVAL) {
        q->next_idxs[l] = fc->queues[idx].next_idxs[l];
        fc->queues[idx].next_idxs[l] = q_idx;
      } else {
        q->next_idxs[l] = fc->head_idx[l];
        fc->head_idx[l] = q_idx;
      }
    }
  }

  q->flags |= FLAG_INSKIPLIST;
}

/** Poll skiplist queues */
static inline unsigned flow_poll_skiplist(struct qman_thread *t, struct flow_cont *fc,
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned cnt;
  uint32_t idx, max_vts;
  int8_t l;
  struct flow_queue *q;

  /* maximum virtual time stamp that can be reached */
  max_vts = t->ts_virtual + (cur_ts - t->ts_real);

  for (cnt = 0; cnt < num;) {
    idx = fc->head_idx[0];

    /* no more queues */
    if (idx == IDXLIST_INVAL) {
      t->ts_virtual = max_vts;
      break;
    }

    q = &fc->queues[idx];

    /* beyond max_vts */
    dprintf("flow_poll_skiplist: next_ts=%u vts=%u rts=%u max_vts=%u cur_ts=%u\n",
        q->next_ts, t->ts_virtual, t->ts_real, max_vts, cur_ts);
    if (!timestamp_lessthaneq(t, q->next_ts, max_vts)) {
      t->ts_virtual = max_vts;
      break;
    }

    /* remove queue from skiplist */
    for (l = 0; l < QMAN_SKIPLIST_LEVELS && fc->head_idx[l] == idx; l++) {
      fc->head_idx[l] = q->next_idxs[l];
    }
    assert((q->flags & FLAG_INSKIPLIST) != 0);
    q->flags &= ~FLAG_INSKIPLIST;

    /* advance virtual timestamp */
    t->ts_virtual = q->next_ts;

    dprintf("flow_poll_skiplist: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n", t, q, idx, q->avail, q->rate, q->flags);

    if (q->avail > 0) {
      flow_queue_fire(t, fc, q, idx, q_ids + cnt, q_bytes + cnt);
      cnt++;
    }
  }

  /* if we reached the limit, update the virtual timestamp correctly */
  if (cnt == num) {
    idx = fc->head_idx[0];
    if (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(t, fc->queues[idx].next_ts, max_vts))
    {
      t->ts_virtual = fc->queues[idx].next_ts;
    } else {
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

/** Level for queue added to skiplist */
static inline uint8_t flow_queue_level(struct qman_thread *t, struct flow_cont *fc)
{
  uint8_t x = (__builtin_ffs(utils_rng_gen32(&t->rng)) - 1) / SKIPLIST_BITS;
  return (x < QMAN_SKIPLIST_LEVELS ? x : QMAN_SKIPLIST_LEVELS - 1);
}

/*****************************************************************************/

static inline void flow_queue_fire(struct qman_thread *t, struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx, unsigned *q_id, uint16_t *q_bytes)
{
  uint32_t bytes;

  assert(q->avail > 0);

  bytes = (q->avail <= q->max_chunk ? q->avail : q->max_chunk);
  q->avail -= bytes;

  dprintf("flow_queue_fire: t=%p q=%p idx=%u gidx=%u bytes=%u avail=%u rate=%u\n", t, q, idx, idx, bytes, q->avail, q->rate);
  if (q->rate > 0) {
    q->next_ts = flow_queue_new_ts(t, q, bytes);
  }

  if (q->avail > 0) {
    flow_queue_activate(t, fc, q, idx);
  }

  *q_bytes = bytes;
  *q_id = idx;

#ifdef FLEXNIC_TRACE_QMAN
  struct flexnic_trace_entry_qman_event evt = {
      .id = *q_id, .bytes = bytes,
    };
  trace_event(FLEXNIC_TRACE_EV_QMEVT, sizeof(evt), &evt);
#endif
}

static inline void flow_queue_activate(struct qman_thread *t, struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx)
{
  if (q->rate == 0) {
    flow_queue_activate_nolimit(fc, q, idx);
  } else {
    flow_queue_activate_skiplist(t, fc, q, idx);
  }
}
