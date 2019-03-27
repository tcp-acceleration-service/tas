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

#define dprintf(...) do { } while (0)

#define FLAG_INSKIPLIST 1
#define FLAG_INNOLIMITL 2

/** Skiplist: bits per level */
#define SKIPLIST_BITS 3
/** Index list: invalid index */
#define IDXLIST_INVAL (-1U)

#define RNG_SEED 0x12345678
#define TIMESTAMP_BITS 32
#define TIMESTAMP_MASK 0xFFFFFFFF

/** Queue state */
struct queue {
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
STATIC_ASSERT((sizeof(struct queue) == 32), queue_size);


/** Actually update queue state: must run on queue's home core */
static inline void set_impl(struct qman_thread *t, uint32_t id, uint32_t rate,
    uint32_t avail, uint16_t max_chunk, uint8_t flags);

/** Add queue to the no limit list */
static inline void queue_activate_nolimit(struct qman_thread *t,
    struct queue *q, uint32_t idx);
static inline unsigned poll_nolimit(struct qman_thread *t, uint32_t cur_ts,
    unsigned num, unsigned *q_ids, uint16_t *q_bytes);

/** Add queue to the skip list list */
static inline void queue_activate_skiplist(struct qman_thread *t,
    struct queue *q, uint32_t idx);
static inline unsigned poll_skiplist(struct qman_thread *t, uint32_t cur_ts,
    unsigned num, unsigned *q_ids, uint16_t *q_bytes);
static inline uint8_t queue_level(struct qman_thread *t);

static inline void queue_fire(struct qman_thread *t,
    struct queue *q, uint32_t idx, unsigned *q_id, uint16_t *q_bytes);
static inline void queue_activate(struct qman_thread *t, struct queue *q,
    uint32_t idx);
static inline uint32_t timestamp(void);
static inline int timestamp_lessthaneq(struct qman_thread *t, uint32_t a,
    uint32_t b);
static inline int64_t rel_time(uint32_t cur_ts, uint32_t ts_in);


int qman_thread_init(struct dataplane_context *ctx)
{
  struct qman_thread *t = &ctx->qman;
  unsigned i;

  if ((t->queues = calloc(1, sizeof(*t->queues) * FLEXNIC_NUM_QMQUEUES))
      == NULL)
  {
    fprintf(stderr, "qman_thread_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < QMAN_SKIPLIST_LEVELS; i++) {
    t->head_idx[i] = IDXLIST_INVAL;
  }
  t->nolimit_head_idx = t->nolimit_tail_idx = IDXLIST_INVAL;
  utils_rng_init(&t->rng, RNG_SEED * ctx->id + ctx->id);

  t->ts_virtual = 0;
  t->ts_real = timestamp();

  return 0;
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

uint32_t qman_next_ts(struct qman_thread *t, uint32_t cur_ts)
{
  uint32_t ts = timestamp();
  uint32_t ret_ts = t->ts_virtual + (ts - t->ts_real);

  if(t->nolimit_head_idx != IDXLIST_INVAL) {
    // Nolimit queue has work - immediate timeout
    fprintf(stderr, "QMan nolimit has work\n");
    return 0;
  }

  uint32_t idx = t->head_idx[0];
  if(idx != IDXLIST_INVAL) {
    struct queue *q = &t->queues[idx];

    if(timestamp_lessthaneq(t, q->next_ts, ret_ts)) {
      // Fired in the past - immediate timeout
      return 0;
    } else {
      // Timeout in the future - return difference
      return rel_time(ret_ts, q->next_ts) / 1000;
    }
  }

  // List empty - no timeout
  return -1;
}

int qman_poll(struct qman_thread *t, unsigned num, unsigned *q_ids,
    uint16_t *q_bytes)
{
  unsigned x, y;
  uint32_t ts = timestamp();

  /* poll nolimit list and skiplist alternating the order between */
  if (t->nolimit_first) {
    x = poll_nolimit(t, ts, num, q_ids, q_bytes);
    y = poll_skiplist(t, ts, num - x, q_ids + x, q_bytes + x);
  } else {
    x = poll_skiplist(t, ts, num, q_ids, q_bytes);
    y = poll_nolimit(t, ts, num - x, q_ids + x, q_bytes + x);
  }
  t->nolimit_first = !t->nolimit_first;

  return x + y;
}

int qman_set(struct qman_thread *t, uint32_t id, uint32_t rate, uint32_t avail,
    uint16_t max_chunk, uint8_t flags)
{
#ifdef FLEXNIC_TRACE_QMAN
  struct flexnic_trace_entry_qman_set evt = {
      .id = id, .rate = rate, .avail = avail, .max_chunk = max_chunk,
      .flags = flags,
    };
  trace_event(FLEXNIC_TRACE_EV_QMSET, sizeof(evt), &evt);
#endif

  dprintf("qman_set: id=%u rate=%u avail=%u max_chunk=%u qidx=%u tid=%u\n",
      id, rate, avail, max_chunk, qidx, tid);

  if (id >= FLEXNIC_NUM_QMQUEUES) {
    fprintf(stderr, "qman_set: invalid queue id: %u >= %u\n", id,
        FLEXNIC_NUM_QMQUEUES);
    return -1;
  }

  set_impl(t, id, rate, avail, max_chunk, flags);

  return 0;
}

/** Actually update queue state: must run on queue's home core */
static void inline set_impl(struct qman_thread *t, uint32_t idx, uint32_t rate,
    uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  struct queue *q = &t->queues[idx];
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

  dprintf("set_impl: t=%p q=%p idx=%u avail=%u rate=%u qflags=%x flags=%x\n", t, q, idx, q->avail, q->rate, q->flags, flags);

  if (new_avail && q->avail > 0
      && ((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0)) {
    queue_activate(t, q, idx);
  }
}

/*****************************************************************************/
/* Managing no-limit queues */

/** Add queue to the no limit list */
static inline void queue_activate_nolimit(struct qman_thread *t,
    struct queue *q, uint32_t idx)
{
  struct queue *q_tail;

  assert((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0);

  dprintf("queue_activate_nolimit: t=%p q=%p avail=%u rate=%u flags=%x\n", t, q, q->avail, q->rate, q->flags);

  q->flags |= FLAG_INNOLIMITL;
  q->next_idxs[0] = IDXLIST_INVAL;
  if (t->nolimit_tail_idx == IDXLIST_INVAL) {
    t->nolimit_head_idx = t->nolimit_tail_idx = idx;
    return;
  }

  q_tail = &t->queues[t->nolimit_tail_idx];
  q_tail->next_idxs[0] = idx;
  t->nolimit_tail_idx = idx;
}

/** Poll no-limit queues */
static inline unsigned poll_nolimit(struct qman_thread *t, uint32_t cur_ts,
    unsigned num, unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned cnt;
  struct queue *q;
  uint32_t idx;

  for (cnt = 0; cnt < num && t->nolimit_head_idx != IDXLIST_INVAL;) {
    idx = t->nolimit_head_idx;
    q = t->queues + idx;

    t->nolimit_head_idx = q->next_idxs[0];
    if (q->next_idxs[0] == IDXLIST_INVAL)
      t->nolimit_tail_idx = IDXLIST_INVAL;

    q->flags &= ~FLAG_INNOLIMITL;
    dprintf("poll_nolimit: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n", t, q, idx, q->avail, q->rate, q->flags);
    if (q->avail > 0) {
      queue_fire(t, q, idx, q_ids + cnt, q_bytes + cnt);
      cnt++;
    }
  }

  return cnt;
}

/*****************************************************************************/
/* Managing skiplist queues */

static inline uint32_t queue_new_ts(struct qman_thread *t, struct queue *q,
    uint32_t bytes)
{
  return t->ts_virtual + ((uint64_t) bytes * 8 * 1000000) / q->rate;
}

/** Add queue to the skip list list */
static inline void queue_activate_skiplist(struct qman_thread *t,
    struct queue *q, uint32_t q_idx)
{
  uint8_t level;
  int8_t l;
  uint32_t preds[QMAN_SKIPLIST_LEVELS];
  uint32_t pred, idx, ts, max_ts;

  assert((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0);

  dprintf("queue_activate_skiplist: t=%p q=%p idx=%u avail=%u rate=%u flags=%x ts_virt=%u next_ts=%u\n", t, q, q_idx, q->avail, q->rate, q->flags,
      t->ts_virtual, q->next_ts);

  /* make sure queue has a reasonable next_ts:
   *  - not in the past
   *  - not more than if it just sent max_chunk at the current rate
   */
  ts = q->next_ts;
  max_ts = queue_new_ts(t, q, q->max_chunk);
  if (timestamp_lessthaneq(t, ts, t->ts_virtual)) {
    ts = q->next_ts = t->ts_virtual;
  } else if (!timestamp_lessthaneq(t, ts, max_ts)) {
    ts = q->next_ts = max_ts;
  }
  q->next_ts = ts;

  /* find predecessors at all levels top-down */
  pred = IDXLIST_INVAL;
  for (l = QMAN_SKIPLIST_LEVELS - 1; l >= 0; l--) {
    idx = (pred != IDXLIST_INVAL ? pred : t->head_idx[l]);
    while (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(t, t->queues[idx].next_ts, ts))
    {
      pred = idx;
      idx = t->queues[idx].next_idxs[l];
    }
    preds[l] = pred;
    dprintf("    pred[%u] = %d\n", l, pred);
  }

  /* determine level for this queue */
  level = queue_level(t);
  dprintf("    level = %u\n", level);

  /* insert into skip-list */
  for (l = QMAN_SKIPLIST_LEVELS - 1; l >= 0; l--) {
    if (l > level) {
      q->next_idxs[l] = IDXLIST_INVAL;
    } else {
      idx = preds[l];
      if (idx != IDXLIST_INVAL) {
        q->next_idxs[l] = t->queues[idx].next_idxs[l];
        t->queues[idx].next_idxs[l] = q_idx;
      } else {
        q->next_idxs[l] = t->head_idx[l];
        t->head_idx[l] = q_idx;
      }
    }
  }

  q->flags |= FLAG_INSKIPLIST;
}

/** Poll skiplist queues */
static inline unsigned poll_skiplist(struct qman_thread *t, uint32_t cur_ts,
    unsigned num, unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned cnt;
  uint32_t idx, max_vts;
  int8_t l;
  struct queue *q;

  /* maximum virtual time stamp that can be reached */
  max_vts = t->ts_virtual + (cur_ts - t->ts_real);

  for (cnt = 0; cnt < num;) {
    idx = t->head_idx[0];

    /* no more queues */
    if (idx == IDXLIST_INVAL) {
      t->ts_virtual = max_vts;
      break;
    }

    q = &t->queues[idx];

    /* beyond max_vts */
    dprintf("poll_skiplist: next_ts=%u vts=%u rts=%u max_vts=%u cur_ts=%u\n",
        q->next_ts, t->ts_virtual, t->ts_real, max_vts, cur_ts);
    if (!timestamp_lessthaneq(t, q->next_ts, max_vts)) {
      t->ts_virtual = max_vts;
      break;
    }

    /* remove queue from skiplist */
    for (l = 0; l < QMAN_SKIPLIST_LEVELS && t->head_idx[l] == idx; l++) {
      t->head_idx[l] = q->next_idxs[l];
    }
    assert((q->flags & FLAG_INSKIPLIST) != 0);
    q->flags &= ~FLAG_INSKIPLIST;

    /* advance virtual timestamp */
    t->ts_virtual = q->next_ts;

    dprintf("poll_skiplist: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n", t, q, idx, q->avail, q->rate, q->flags);

    if (q->avail > 0) {
      queue_fire(t, q, idx, q_ids + cnt, q_bytes + cnt);
      cnt++;
    }
  }

  /* if we reached the limit, update the virtual timestamp correctly */
  if (cnt == num) {
    idx = t->head_idx[0];
    if (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(t, t->queues[idx].next_ts, max_vts))
    {
      t->ts_virtual = t->queues[idx].next_ts;
    } else {
      t->ts_virtual = max_vts;
    }
  }

  t->ts_real = cur_ts;
  return cnt;
}

/** Level for queue added to skiplist */
static inline uint8_t queue_level(struct qman_thread *t)
{
  uint8_t x = (__builtin_ffs(utils_rng_gen32(&t->rng)) - 1) / SKIPLIST_BITS;
  return (x < QMAN_SKIPLIST_LEVELS ? x : QMAN_SKIPLIST_LEVELS - 1);
}

/*****************************************************************************/

static inline void queue_fire(struct qman_thread *t,
    struct queue *q, uint32_t idx, unsigned *q_id, uint16_t *q_bytes)
{
  uint32_t bytes;

  assert(q->avail > 0);

  bytes = (q->avail <= q->max_chunk ? q->avail : q->max_chunk);
  q->avail -= bytes;

  dprintf("queue_fire: t=%p q=%p idx=%u gidx=%u bytes=%u avail=%u rate=%u\n", t, q, idx, idx, bytes, q->avail, q->rate);
  if (q->rate > 0) {
    q->next_ts = queue_new_ts(t, q, bytes);
  }

  if (q->avail > 0) {
    queue_activate(t, q, idx);
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

static inline void queue_activate(struct qman_thread *t, struct queue *q,
    uint32_t idx)
{
  if (q->rate == 0) {
    queue_activate_nolimit(t, q, idx);
  } else {
    queue_activate_skiplist(t, q, idx);
  }
}

static inline uint32_t timestamp(void)
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

  if (cur_ts < middle) {
    /* negative interval is split in half */
    start = (cur_ts - middle) & TIMESTAMP_MASK;
    end = (1ULL << TIMESTAMP_BITS);
    if (start <= ts && ts < end) {
      /* in first half of negative interval, smallest timestamps */
      return ts - start - middle;
    } else {
      /* in second half or in positive interval */
      return ts - cur_ts;
    }
  } else if (cur_ts == middle) {
    /* intervals not split */
    return ts - cur_ts;
  } else {
    /* higher interval is split */
    start = 0;
    end = ((cur_ts + middle) & TIMESTAMP_MASK) + 1;
    if (start <= cur_ts && ts < end) {
      /* in second half of positive interval, largest timestamps */
      return ts + ((1ULL << TIMESTAMP_BITS) - cur_ts);
    } else {
      /* in negative interval or first half of positive interval */
      return ts - cur_ts;
    }
  }
}

static inline int timestamp_lessthaneq(struct qman_thread *t, uint32_t a,
    uint32_t b)
{
  return rel_time(t->ts_virtual, a) <= rel_time(t->ts_virtual, b);
}
