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

#ifndef UTILS_CRIC_H_
#define UTILS_CRIC_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/* Calculates one or two (in case of wrap-around) ranges of length `len' at
 * `pos' in circular buffer of length `b_len' at address `b_base'. */
static inline int circ_range(void **b1, size_t *l1, void **b2, void *b_base,
    size_t b_len, size_t pos, size_t len)
{
  uint8_t *b = b_base;
  size_t l;

  if (pos + len <= b_len) {
    //printf("circ_range case1: len=%zu l1=%zu b_len=%zu pos=%zu\n", len, len, b_len, pos);
    *b1 = b + pos;
    *l1 = len;
    *b2 = NULL;
    return 0;
  } else {
    l = b_len - pos;
    //printf("circ_range case2: len=%zu l1=%zu b_len=%zu pos=%zu\n", len, l, b_len, pos);

    *b1 = b + pos;
    *l1 = l;
    *b2 = b;
    return 1;
  }
}

/* Increment position `pos' in circular buffer of length `len' by
 * `off'. */
static inline size_t circ_offset(size_t pos, size_t len, size_t off)
{
  pos += off;
  if (pos >= len) {
    pos -= len;
  }
  return pos;
}

static inline int circ_in_interval(size_t start, size_t end, size_t len,
    size_t pos)
{
  if (start < end) {
    return (pos >= start && pos <= end);
  } else {
    return (pos >= start) || (pos <= end);
  }
}

static inline void circ_read(void *dst, void *b_base, size_t b_len,
    size_t pos, size_t len)
{
  uint8_t *d = dst;
  void *b1, *b2;
  size_t l;

  if (circ_range(&b1, &l, &b2, b_base, b_len, pos, len) == 0) {
    memcpy(d, b1, l);
  } else {
    memcpy(d, b1, l);
    memcpy(d + l, b2, len - l);
  }
}

static inline void circ_write(const void *dst, void *b_base, size_t b_len,
    size_t pos, size_t len)
{
  const uint8_t *d = dst;
  void *b1, *b2;
  size_t l;

  if (circ_range(&b1, &l, &b2, b_base, b_len, pos, len) == 0) {
    memcpy(b1, d, l);
  } else {
    memcpy(b1, d, l);
    memcpy(b2, d + l, len - l);
  }
}

static inline void split_write(const void *src, size_t len, void *buf_1,
    size_t len_1, void *buf_2, size_t len_2, size_t off)
{
  size_t l;

  assert(len + off <= len_1 + len_2);
  if (off + len <= len_1) {
    /* only in first half */
    memcpy((uint8_t *) buf_1 + off, src, len);
  } else if (off >= len_1) {
    /* only in second half */
    memcpy((uint8_t *) buf_2 + (off - len_1), src, len);
  } else {
    /* spread over both halves */
    l = len_1 - off;
    memcpy((uint8_t *) buf_1 + off, src, l);
    memcpy(buf_2, (const uint8_t *) src + l, len - l);
  }
}

static inline void split_read(void *dst, size_t len, const void *buf_1,
    size_t len_1, const void *buf_2, size_t len_2, size_t off)
{
  size_t l;

  assert(len + off <= len_1 + len_2);
  if (off + len <= len_1) {
    /* only in first half */
    memcpy(dst, (const uint8_t *) buf_1 + off, len);
  } else if (off >= len_1) {
    /* only in second half */
    memcpy(dst, (const uint8_t *) buf_2 + (off - len_1), len);
  } else {
    /* spread over both halves */
    l = len_1 - off;
    memcpy(dst, (const uint8_t *) buf_1 + off, l);
    memcpy((uint8_t *) dst + l, buf_2, len - l);
  }
}

#endif /* ndef UTILS_CIRC_H_ */
