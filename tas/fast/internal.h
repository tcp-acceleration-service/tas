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

// #define FLEXNIC_TRACING
#ifdef FLEXNIC_TRACING
#   include <tas_trace.h>
#   define FLEXNIC_TRACE_RX
#   define FLEXNIC_TRACE_TX
#   define FLEXNIC_TRACE_DMA
#   define FLEXNIC_TRACE_QMAN
#   define FLEXNIC_TRACE_LEN (1024 * 256)
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

#define TCP_MSS 1448

/** Index list: invalid index */
#define IDXLIST_INVAL (-1U)

/** Qman functions */
int tas_qman_thread_init(struct dataplane_context *ctx);
int tas_qman_poll(struct dataplane_context *ctx, unsigned num, unsigned *vm_id, 
    unsigned *q_ids, uint16_t *q_bytes);
int tas_qman_set(struct qman_thread *t, uint32_t vm_id, uint32_t flow_id, uint32_t rate,
    uint32_t avail, uint16_t max_chunk, uint8_t flags);
uint32_t tas_qman_timestamp(uint64_t tsc);
uint32_t tas_qman_next_ts(struct qman_thread *t, uint32_t cur_ts);
/** Helper functions for unit tests */
uint32_t qman_vm_get_avail(struct dataplane_context *ctx, uint32_t vm_id);
void qman_free_vm_cont(struct dataplane_context *ctx);

void *util_create_shmsiszed(const char *name, size_t size, void *addr,
    int *pfd);

#endif /* ndef INTERNAL_H_ */
