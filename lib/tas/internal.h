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

#include <tas_ll.h>
#include <tas_memif.h>
#include <utils_circ.h>

#define OPAQUE_PTR(x) ((void *) (uintptr_t) (x))
#define OPAQUE(x) ((uintptr_t) (x))

/* ctx->flags of internal state: */
/** Set whenever context_poll is called */
#define CTX_FLAG_POLL_CALLED 1
/** Set whenever context_poll finds events in queue */
#define CTX_FLAG_POLL_EVENTS 2
/** Indicates that the grace period for blocking is currently running with a
 * caller waiting for permission to block. */
#define CTX_FLAG_WANTWAIT 4
/** Grace period is over, after polling once more, blocking will be allowed. */
#define CTX_FLAG_LASTWAIT 8

#define CONN_FLAG_TXEOS 1
#define CONN_FLAG_TXEOS_ALLOC 2
#define CONN_FLAG_TXEOS_ACK 4
#define CONN_FLAG_RXEOS 8

enum conn_state {
  CONN_CLOSED,
  CONN_OPEN_REQUESTED,
  CONN_ACCEPT_REQUESTED,
  CONN_OPEN,
  CONN_CLOSE_REQUESTED,
};

extern void *flexnic_mem;
extern struct flexnic_info *flexnic_info;
extern int flexnic_evfd[FLEXTCP_MAX_FTCPCORES];

int flextcp_kernel_connect(void);
int flextcp_kernel_newctx(struct flextcp_context *ctx);
void flextcp_kernel_kick(void);

int flextcp_context_tx_alloc(struct flextcp_context *ctx,
    struct flextcp_pl_atx **atx, uint16_t core);
void flextcp_context_tx_done(struct flextcp_context *ctx, uint16_t core);

uint32_t flextcp_conn_txbuf_available(struct flextcp_connection *conn);
int flextcp_conn_pushtxeos(struct flextcp_context *ctx,
        struct flextcp_connection *conn);

#endif /* ndef INTERNAL_H_ */
