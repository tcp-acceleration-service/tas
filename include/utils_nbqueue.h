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

#ifndef UTILS_NBQUEUE_H_
#define UTILS_NBQUEUE_H_

#include <assert.h>
#include <pthread.h>

struct nbqueue_el {
  struct nbqueue_el *next;
};

struct nbqueue {
  struct nbqueue_el *head;
  pthread_mutex_t mutex;
};

static inline void nbqueue_init(struct nbqueue *nbq)
{
  nbq->head = NULL;
  pthread_mutex_init(&nbq->mutex, NULL);
}

static inline void nbqueue_enq(struct nbqueue *nbq, struct nbqueue_el *el)
{
  pthread_mutex_lock(&nbq->mutex);
  el->next = nbq->head;
  nbq->head = el;
  pthread_mutex_unlock(&nbq->mutex);
}

static inline void *nbqueue_deq(struct nbqueue *nbq)
{
  struct nbqueue_el *el, *el_p;
  if (nbq->head == NULL) {
    return NULL;
  }

  pthread_mutex_lock(&nbq->mutex);

  for (el = nbq->head, el_p = NULL; el != NULL && el->next != NULL;
      el = el->next)
  {
    el_p = el;
  }
  assert(el->next == NULL);

  if (el != NULL) {
    if (el_p != NULL) {
      el_p->next = NULL;
    } else {
      nbq->head = NULL;
    }
  }

  pthread_mutex_unlock(&nbq->mutex);

  return el;
}

#endif /* ndef UTILS_NBQUEUE_H_ */
