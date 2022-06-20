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
 * Universal functions for queue manager implementation
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

#define RNG_SEED 0x12345678
#define TIMESTAMP_BITS 32
#define TIMESTAMP_MASK 0xFFFFFFFF

static inline int64_t rel_time(uint32_t cur_ts, uint32_t ts_in);


int qman_thread_init(struct dataplane_context *ctx)
{
  struct qman_thread *t = &ctx->qman;
  
  if (appcont_init(t) != 0)
  {
    fprintf(stderr, "qman_thread_init: app_cont init failed\n");
    return -1;
  }
  
  utils_rng_init(&t->rng, RNG_SEED * ctx->id + ctx->id);
  t->ts_virtual = 0;
  t->ts_real = timestamp();

  return 0;
}

int qman_poll(struct qman_thread *t, unsigned num, unsigned *app_id, 
    unsigned *q_ids, uint16_t *q_bytes)
{
  int ret;
  struct app_cont *ac = t->a_cont;

  ret = app_qman_poll(t, ac, num, app_id, q_ids, q_bytes);
  return ret;
}

int qman_set(struct qman_thread *t, uint32_t app_id, uint32_t flow_id, uint32_t rate,
    uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  int ret;
  ret = app_qman_set(t, app_id, flow_id, rate, avail, max_chunk, flags);
  return ret;
}

uint32_t qman_next_ts(struct qman_thread *t, uint32_t cur_ts)
{
  struct app_queue *aq;
  struct flow_cont *fc;
  uint32_t ts = timestamp();
  uint32_t ret_ts = t->ts_virtual + (ts - t->ts_real);
  struct app_cont *ac = t->a_cont;

  if (ac->head_idx == IDXLIST_INVAL) {
    return -1;
  }

  aq = &ac->queues[ac->head_idx];
  fc = aq->f_cont;

  if(fc->nolimit_head_idx != IDXLIST_INVAL) {
    // Nolimit queue has work - immediate timeout
    fprintf(stderr, "QMan nolimit has work\n");
    return 0;
  }

  uint32_t idx = fc->head_idx[0];
  if(idx != IDXLIST_INVAL) {
    struct flow_queue *q = &fc->queues[idx];

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

int timestamp_lessthaneq(struct qman_thread *t, uint32_t a,
    uint32_t b)
{
  return rel_time(t->ts_virtual, a) <= rel_time(t->ts_virtual, b);
}
