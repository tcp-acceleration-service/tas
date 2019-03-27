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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <utils.h>
#include <utils_timeout.h>

/** actual number of bits used for timestamps (note this is used in struct
 * timeout). */
#define TIMEOUT_BITS 28
/** bitmask for valid bits used for timestamps */
#define TIMEOUT_MASK ((1 << TIMEOUT_BITS) - 1)

/** maximum number of timestamps to handle per call to timeout_poll() */
#define MAX_TIMEOUTS 64

/** rdtsc cycles per microsecond */
static uint64_t tsc_per_us = 0;

/** Move due timeouts from #timeouts to due list. */
static inline void move_due_timeouts(struct timeout_manager *mgr,
    uint32_t cur_ts);
/** Is this timeout due? */
static inline int timeout_due(struct timeout *to, uint32_t ts);

/** Timestamp in microseconds (full 32 bits) */
static inline uint32_t timestamp_us_long(void);
/** #TIMEOUT_BITS bits Timestamp in microseconds */
static inline uint32_t timestamp_us(void);
/** "relative" time ignoring wrap arounds */
static inline int32_t rel_time(uint32_t cur, struct timeout *to);
/** Estimate tsc frequency: fills in tsc_per_us */
static inline void calibrate_tsc(void);

int util_timeout_init(struct timeout_manager *mgr,
    void (*handler)(struct timeout *, uint8_t, void *), void *handler_opaque)
{
  calibrate_tsc();
  memset(mgr, 0, sizeof(*mgr));
  mgr->handler = handler;
  mgr->handler_opaque = handler_opaque;
  return 0;
}

uint32_t util_timeout_time_us(void)
{
  if (tsc_per_us == 0)
    calibrate_tsc();
  return timestamp_us_long();
}

void util_timeout_poll(struct timeout_manager *mgr)
{
  util_timeout_poll_ts(mgr, timestamp_us());
}

void util_timeout_poll_ts(struct timeout_manager *mgr, uint32_t cur_ts)
{
  unsigned num = 0;
  struct timeout *to;

  cur_ts &= TIMEOUT_MASK;

  /* check for potential due timeouts in #timeouts */
  move_due_timeouts(mgr, cur_ts);

  /* process due queue */
  while ((to = mgr->due_first) != NULL && num < MAX_TIMEOUTS) {
    mgr->due_first = to->next;
    if (mgr->due_first != NULL) {
      mgr->due_first->prev = NULL;
    } else {
      mgr->due_last = NULL;
    }

    mgr->handler(to, to->timeout_type >> TIMEOUT_BITS, mgr->handler_opaque);

    num++;
  }

}

void util_timeout_arm(struct timeout_manager *mgr, struct timeout *to,
    uint32_t us, uint8_t type)
{
  util_timeout_arm_ts(mgr, to, us, type, timestamp_us());
}

void util_timeout_arm_ts(struct timeout_manager *mgr, struct timeout *to,
    uint32_t us, uint8_t type, uint32_t cur_ts)
{
  struct timeout *tp, *tn;

  cur_ts &= TIMEOUT_MASK;

  /* make sure #us is not out of range */
  if (us >= (1 << (TIMEOUT_BITS - 1))) {
    fprintf(stderr, "timeout_arm: specified timeout is out of range (needs to "
        "be < %u, but got %u)\n", (1 << (TIMEOUT_BITS - 1)), us);
    abort();
  }

  /* step 1: move all due timeouts to due queue */
  move_due_timeouts(mgr, cur_ts);

  /* step 2: find predecessor and successor in list */
  for (tp = mgr->timeouts_last; tp != NULL && rel_time(cur_ts, tp) > us;
      tp = tp->prev);
  tn = (tp != NULL ? tp->next : mgr->timeouts_first);

  /* step 3: insert */
  to->timeout_type = ((uint32_t) type) << TIMEOUT_BITS;
  to->timeout_type |= (cur_ts + us) & TIMEOUT_MASK;
  to->next = tn;
  to->prev = tp;
  if (tp == NULL) {
    mgr->timeouts_first = to;
  } else {
    tp->next = to;
  }
  if (tn == NULL) {
    mgr->timeouts_last = to;
  } else {
    tn->prev = to;
  }
}

void util_timeout_disarm(struct timeout_manager *mgr, struct timeout *to)
{
  struct timeout *prev, *next;

  prev = to->prev;
  next = to->next;
  if (prev == NULL) {
    if (mgr->timeouts_first == to) {
      mgr->timeouts_first = next;
    } else if (mgr->due_first == to) {
      mgr->due_first = next;
    } else {
      fprintf(stderr, "timeout_disarm: timeout neither in timeouts_first nor "
          "due_first\n");
      abort();
    }
  } else {
    prev->next = next;
  }

  if (next == NULL) {
    if (mgr->timeouts_last == to) {
      mgr->timeouts_last = prev;
    } else if (mgr->due_last == to) {
      mgr->due_last = prev;
    } else {
      fprintf(stderr, "timeout_disarm: timeout neither in timeouts_last nor "
          "due_last\n");
      abort();
    }
  } else {
    next->prev = prev;
  }
}

uint32_t util_timeout_next(struct timeout_manager *mgr, uint32_t cur_ts)
{
  if(mgr->due_first != NULL) {
    // We have timeouts due immediately
    return 0;
  }

  if(mgr->timeouts_first == NULL) {
    // Nothing due
    return -1U;
  }

  cur_ts &= TIMEOUT_MASK;
  int32_t next = rel_time(cur_ts, mgr->timeouts_first);
  return (next < 0 ? 0 : next);
}

static inline void move_due_timeouts(struct timeout_manager *mgr, uint32_t cur_ts)
{
  struct timeout *to, *next;

  while ((to = mgr->timeouts_first) != NULL &&
      timeout_due(mgr->timeouts_first, cur_ts))
  {
    /* remove from timeouts list */
    mgr->timeouts_first = next = to->next;
    if (next != NULL) {
      next->prev = NULL;
    } else {
      mgr->timeouts_last = NULL;
    }

    /* add to due list */
    to->next = NULL;
    to->prev = mgr->due_last;
    if (mgr->due_last == NULL) {
      mgr->due_first = to;
    } else {
      mgr->due_last->next = to;
    }
    mgr->due_last = to;
  }
}

static inline int timeout_due(struct timeout *to, uint32_t ts)
{
  return rel_time(ts, to) <= 0;
}

static inline uint32_t timestamp_us_long(void)
{
  return util_rdtsc() / tsc_per_us;
}

static inline uint32_t timestamp_us(void)
{
  return timestamp_us_long() & TIMEOUT_MASK;
}

static inline int32_t rel_time(uint32_t cur_ts, struct timeout *to)
{
  const uint32_t middle = (1 << (TIMEOUT_BITS - 1));
  uint32_t to_ts = to->timeout_type & TIMEOUT_MASK;
  uint32_t start, end;

  if (cur_ts < middle) {
    /* negative interval is split in half */
    start = (cur_ts - middle) & TIMEOUT_MASK;
    end = (1 << TIMEOUT_BITS);
    if (start <= to_ts && to_ts < end) {
      /* in first half of negative interval, smallest timestamps */
      return to_ts - start - middle;
    } else {
      /* in second half or in positive interval */
      return to_ts - cur_ts;
    }
  } else if (cur_ts == middle) {
    /* intervals not split */
    return to_ts - cur_ts;
  } else {
    /* higher interval is split */
    start = 0;
    end = ((cur_ts + middle) & TIMEOUT_MASK) + 1;
    if (start <= cur_ts && to_ts < end) {
      /* in second half of positive interval, largest timestamps */
      return to_ts + ((1 << TIMEOUT_BITS) - cur_ts);
    } else {
      /* in negative interval or first half of positive interval */
      return to_ts - cur_ts;
    }
  }
}

/** Estimate tsc frequency: fills in tsc_per_us */
static inline void calibrate_tsc(void)
{
  struct timespec ts_before, ts_after;
  uint64_t tsc;
  double freq;

  if (tsc_per_us != 0)
    return;

  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts_before) != 0) {
    fprintf(stderr, "calibrate_tsc: clock_gettime(CLOCK_MONOTONIC_RAW) "
        "failed\n");
    abort();
  }

  tsc = util_rdtsc();
  usleep(10000);
  tsc = util_rdtsc() - tsc;

  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts_after) != 0) {
    fprintf(stderr, "calibrate_tsc: clock_gettime(CLOCK_MONOTONIC_RAW) "
        "failed\n");
    abort();
  }

  freq = ((ts_after.tv_sec * 1000000UL) + (ts_after.tv_nsec / 1000)) -
    ((ts_before.tv_sec * 1000000UL) + (ts_before.tv_nsec / 1000));
  tsc_per_us = tsc / freq;
}
