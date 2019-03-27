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
#include <stdlib.h>

#include <tas.h>
#include "internal.h"

struct packetmem_handle {
  uintptr_t base;
  size_t len;

  struct packetmem_handle *next;
};

static inline struct packetmem_handle *ph_alloc(void);
static inline void ph_free(struct packetmem_handle *ph);
static inline void merge_items(struct packetmem_handle *ph_prev);

static struct packetmem_handle *freelist;

int packetmem_init(void)
{
  struct packetmem_handle *ph;

  if ((ph = ph_alloc()) == NULL) {
    fprintf(stderr, "packetmem_init: ph_alloc failed\n");
    return -1;
  }

  ph->base = 0;
  ph->len = tas_info->dma_mem_size;
  ph->next = NULL;
  freelist = ph;

  return 0;
}

int packetmem_alloc(size_t length, uintptr_t *off,
    struct packetmem_handle **handle)
{
  struct packetmem_handle *ph, *ph_prev, *ph_new;

  /* look for first fit */
  ph_prev = NULL;
  ph = freelist;
  while (ph != NULL && ph->len < length) {
    ph_prev = ph;
    ph = ph->next;
  }

  /* didn't find a fit */
  if (ph == NULL) {
    return -1;
  }

  if (ph->len == length) {
    /* simple case, don't need to split this handle */

    /* pointer to previous next pointer for removal */
    if (ph_prev == NULL) {
      freelist = ph->next;
    } else {
      ph_prev->next = ph->next;
    }

    ph_new = ph;
  } else {
    /* need to split */

    /* new packetmem handle for splitting */
    if ((ph_new = ph_alloc()) == NULL) {
      fprintf(stderr, "packetmem_alloc: ph_alloc failed\n");
      return -1;
    }

    ph_new->base = ph->base;
    ph_new->len = length;
    ph_new->next = NULL;

    ph->base += length;
    ph->len -= length;
  }

  *handle = ph_new;
  *off = ph_new->base;

  return 0;
}

void packetmem_free(struct packetmem_handle *handle)
{
  struct packetmem_handle *ph, *ph_prev;

  /* look for first successor */
  ph_prev = NULL;
  ph = freelist;
  while (ph != NULL && ph->next != NULL && ph->next->base < handle->base) {
    ph_prev = ph;
    ph = ph->next;
  }

  /* add to list */
  if (ph_prev == NULL) {
    handle->next = freelist;
    freelist = handle;
  } else {
    handle->next = ph_prev->next;
    ph_prev->next = handle;
  }

  /* merge items if necessary */
  merge_items(ph_prev);
}

/** Merge handles around newly inserted item (pointer to predecessor or NULL
 * passed).
 */
static inline void merge_items(struct packetmem_handle *ph_prev)
{
  struct packetmem_handle *ph, *ph_next;

  /* try to merge with predecessor if there is one */
  if (ph_prev != NULL) {
    ph = ph_prev->next;
    if (ph_prev->base + ph_prev->len == ph->base) {
      /* merge with predecessor */
      ph_prev->next = ph->next;
      ph_prev->len += ph->len;
      ph_free(ph);
      ph = ph_prev;
    }
  } else {
    ph = freelist;
  }

  /* try to merge with successor if there is one */
  ph_next = ph->next;
  if (ph_next != NULL && ph->base + ph->len == ph_next->base) {
    ph->len += ph_next->len;
    ph->next = ph_next->next;
    ph_free(ph_next);
  }
}

static inline struct packetmem_handle *ph_alloc(void)
{
  return malloc(sizeof(struct packetmem_handle));
}

static inline void ph_free(struct packetmem_handle *ph)
{
  free(ph);
}
