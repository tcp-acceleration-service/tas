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

#ifndef FASTPATH_H_
#define FASTPATH_H_

#include <stdbool.h>
#include <stdint.h>

#include <rte_interrupts.h>

#include <tas_memif.h>
#include <utils_rng.h>

#define BATCH_SIZE 16
#define BUFCACHE_SIZE 128
#define TXBUF_SIZE (2 * BATCH_SIZE)

#define FLAG_ACTIVE 1
#define MAX_POLL_ROUNDS 100
#define MAX_NULL_ROUNDS 50

struct network_thread {
  struct rte_mempool *pool;
  uint16_t queue_id;
};

/** Skiplist: #levels */
#define QMAN_SKIPLIST_LEVELS 4

struct qman_thread {
  /* modified by owner thread */
  /************************************/
  struct vm_qman *vqman;
  uint32_t ts_real;
  uint32_t ts_virtual;
  struct utils_rng rng;
};

struct polled_context {
  uint32_t id;
  uint32_t vmid;
  uint32_t next;
  uint32_t prev;
  uint16_t flags;
  uint16_t null_rounds;
};

struct polled_vm {
  uint32_t id;
  uint32_t next;
  uint32_t prev;
  uint16_t flags;
  uint32_t poll_next_ctx;

  /* polled contexts for each app */
  uint32_t act_ctx_head;
  uint32_t act_ctx_tail;
  struct polled_context ctxs[FLEXNIC_PL_APPST_CTX_NUM];
};

struct dataplane_context {
  struct network_thread net;
  struct qman_thread qman;
  struct rte_ring *qman_fwd_ring;
  uint16_t id;
  int evfd;
  struct rte_epoll_event ev;

  /********************************************************/
  /* arx cache */
  struct flextcp_pl_arx arx_cache[BATCH_SIZE];
  uint16_t arx_ctx[BATCH_SIZE];
  uint16_t arx_vm[BATCH_SIZE];
  uint16_t arx_num;

  /********************************************************/
  /* send buffer */
  struct network_buf_handle *tx_handles[TXBUF_SIZE];
  uint16_t tx_num;

  /********************************************************/
  /* polling queues */
  /* polling rounds counter until we have to poll every queue */
  uint16_t poll_rounds;
  uint32_t poll_next_vm;
  uint32_t act_head;
  uint32_t act_tail;
  struct polled_vm polled_vms[FLEXNIC_PL_APPST_NUM];  

  /********************************************************/
  /* pre-allocated buffers for polling doorbells and queue manager */
  struct network_buf_handle *bufcache_handles[BUFCACHE_SIZE];
  uint16_t bufcache_num;
  uint16_t bufcache_head;

  uint64_t loadmon_cyc_busy;

  uint64_t kernel_drop;
#ifdef DATAPLANE_STATS
  /********************************************************/
  /* Stats */
  uint64_t stat_qm_poll;
  uint64_t stat_qm_empty;
  uint64_t stat_qm_total;

  uint64_t stat_rx_poll;
  uint64_t stat_rx_empty;
  uint64_t stat_rx_total;

  uint64_t stat_qs_poll;
  uint64_t stat_qs_empty;
  uint64_t stat_qs_total;

  uint64_t stat_cyc_db;
  uint64_t stat_cyc_qm;
  uint64_t stat_cyc_rx;
  uint64_t stat_cyc_qs;
#endif
};

extern struct dataplane_context **ctxs;

int dataplane_init(void);
int dataplane_context_init(struct dataplane_context *ctx);
void dataplane_context_destroy(struct dataplane_context *ctx);
void dataplane_loop(struct dataplane_context *ctx);
#ifdef DATAPLANE_STATS
void dataplane_dump_stats(void);
#endif

#endif /* ndef FASTPATH_H_ */
