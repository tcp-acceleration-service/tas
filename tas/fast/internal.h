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

#ifndef INTERNAL_H_
#define INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

#include <rte_config.h>
#include <rte_ether.h>

#define BUFFER_SIZE 2048

//#define FLEXNIC_TRACING
#ifdef FLEXNIC_TRACING
#   include <tas_trace.h>
#   define FLEXNIC_TRACE_RX
#   define FLEXNIC_TRACE_TX
#   define FLEXNIC_TRACE_DMA
#   define FLEXNIC_TRACE_QMAN
#   define FLEXNIC_TRACE_LEN (1024 * 1024 * 32)
    int trace_thread_init(uint16_t id);
    int trace_event(uint16_t type, uint16_t length, const void *buf);
    int trace_event2(uint16_t type, uint16_t len_1, const void *buf_1,
        uint16_t len_2, const void *buf_2);
#endif
//#define DATAPLANE_STATS

extern int exited;
extern unsigned fp_cores_max;
extern volatile unsigned fp_cores_cur;
extern volatile unsigned fp_scale_to;


#include "dma.h"
#include "network.h"

#define QMAN_SET_RATE     (1 << 0)
#define QMAN_SET_MAXCHUNK (1 << 1)
#define QMAN_SET_AVAIL    (1 << 3)
#define QMAN_ADD_AVAIL    (1 << 4)

#define dprintf(...) do { } while (0)

#define FLAG_INSKIPLIST 1
#define FLAG_INNOLIMITL 2

/** Index list: invalid index */
#define IDXLIST_INVAL (-1U)

/** Skiplist: bits per level */
#define SKIPLIST_BITS 3

/** Queue container for an application */
struct app_cont {
  /** Application queue */
  struct app_queue *queues;
  /** Idx of head of queue */
  uint32_t head_idx;
  /** Idx of tail of queue */
  uint32_t tail_idx;
};

/** Queue container for a flow **/
struct flow_cont {
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

/** Queue state for application */
struct app_queue {
  /** Next pointer */
  uint32_t next_idx;
  /** Pointer to container with flows for this app */
  struct flow_cont *f_cont;
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

/** Init functions for the different scheduler hierarchies */
int flowcont_init(struct app_queue *aq);
int appcont_init(struct qman_thread *t);
int qman_thread_init(struct dataplane_context *ctx);

/** Poll functions for the different scheduler hierarchies */
int flow_qman_poll(struct qman_thread *t, struct flow_cont *fc,
    unsigned num, unsigned *q_ids, uint16_t *q_bytes);
int app_qman_poll(struct qman_thread *t, struct app_cont *ac,
    unsigned num, unsigned *app_id, unsigned *q_ids, uint16_t *q_bytes);
int qman_poll(struct qman_thread *t, unsigned num, unsigned *app_id, 
    unsigned *q_ids, uint16_t *q_bytes);

/** Set functions for the different scheduler hierarchies */
int flow_qman_set(struct qman_thread *t, struct flow_cont *fc, uint32_t flow_id,
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
int app_qman_set(struct qman_thread *t, uint32_t app_id, uint32_t flow_id,
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
int qman_set(struct qman_thread *t, uint32_t app_id, uint32_t flow_id, uint32_t rate,
    uint32_t avail, uint16_t max_chunk, uint8_t flags);

uint32_t timestamp(void);
uint32_t qman_timestamp(uint64_t tsc);
uint32_t qman_next_ts(struct qman_thread *t, uint32_t cur_ts);
int timestamp_lessthaneq(struct qman_thread *t, uint32_t a,
    uint32_t b);

void *util_create_shmsiszed(const char *name, size_t size, void *addr);

#endif /* ndef INTERNAL_H_ */
