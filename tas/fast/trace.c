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
#include <assert.h>

#include <tas_trace.h>
#include <utils.h>
#include "internal.h"

#ifdef FLEXNIC_TRACING

struct trace {
  struct flexnic_trace_header *hdr;
  void  *base;
  size_t len;
  size_t pos;
  uint32_t seq;
};

static __thread struct trace *trace;

static inline void copy_to_pos(struct trace *t, size_t pos, size_t len,
    const void *src);

int trace_thread_init(uint16_t id)
{
  struct trace *t;
  char name[64];

  t = trace;
  if (t != NULL) {
    fprintf(stderr, "trace_thread_init: trace already initialized for "
        "thread\n");
    return -1;
  }

  if ((t = malloc(sizeof(*t))) == NULL) {
    fprintf(stderr, "trace_thread_init: malloc failed\n");
    return -1;
  }

  snprintf(name, sizeof(name), FLEXNIC_TRACE_NAME, id);
  if ((t->hdr = util_create_shmsiszed(name, FLEXNIC_TRACE_LEN, NULL))
      == NULL)
  {
    free(t);
    return -1;
  }

  t->base = t->hdr + 1;
  t->len = FLEXNIC_TRACE_LEN - sizeof(*t->hdr);
  t->pos = 0;
  t->seq = 0;

  t->hdr->end_last = 0;
  t->hdr->length = t->len;

  trace = t;
  return 0;
}

int trace_event(uint16_t type, uint16_t len, const void *buf)
{
  return trace_event2(type, len, buf, 0, NULL);
}

int trace_event2(uint16_t type, uint16_t len_1, const void *buf_1,
    uint16_t len_2, const void *buf_2)
{
  struct flexnic_trace_entry_head teh;
  struct flexnic_trace_entry_tail tet;
  struct timespec ts;
  struct trace *t;
  uint64_t newpos;
  uint16_t len = len_1 + len_2;

  t = trace;
  if (t == NULL) {
    fprintf(stderr, "trace_event: thread trace not initialized yet\n");
    return -1;
  }

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    fprintf(stderr, "trace_event: clock_gettime failed\n");
    return -1;
  }

  teh.ts = (uint64_t) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  teh.seq = t->seq++;
  teh.type = type;
  teh.length = len;
  tet.length = len;

  copy_to_pos(t, t->pos,                     sizeof(teh), &teh);
  copy_to_pos(t, t->pos + sizeof(teh),       len_1,       buf_1);
  if (len_2 > 0) {
    copy_to_pos(t, t->pos + sizeof(teh) + len_1, len_2,   buf_2);
  }
  copy_to_pos(t, t->pos + sizeof(teh) + len, sizeof(tet), &tet);

  newpos = t->pos + len + sizeof(teh) + sizeof(tet);
  if (newpos >= t->len) {
    newpos -= t->len;
  }
  t->pos = newpos;
  MEM_BARRIER();
  t->hdr->end_last = newpos;

  return 0;
}

static inline void copy_to_pos(struct trace *t, size_t pos, size_t len,
    const void *src)
{
  size_t first;

  if (pos >= t->len) {
    pos -= t->len;
  }
  assert(pos < t->len);

  if (pos + len <= t->len) {
    memcpy((uint8_t *) t->base + pos, src, len);
  } else {
    first = t->len - pos;
    memcpy((uint8_t *) t->base + pos, src, first);
    memcpy(t->base, (uint8_t *) src + first, len - first);
  }
}
#endif
