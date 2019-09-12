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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tas_ll.h>
#include <kernel_appif.h>
#include "internal.h"

static void connection_init(struct flextcp_connection *conn);

static inline void conn_mark_bump(struct flextcp_context *ctx,
    struct flextcp_connection *conn);
static inline uint32_t conn_tx_allocbytes(struct flextcp_connection *conn);
static inline uint32_t conn_tx_sendbytes(struct flextcp_connection *conn);

int flextcp_listen_open(struct flextcp_context *ctx,
    struct flextcp_listener *lst, uint16_t port, uint32_t backlog,
    uint32_t flags)
{
  uint32_t pos = ctx->kin_head;
  struct kernel_appout *kin = ctx->kin_base;
  uint32_t f = 0;

  memset(lst, 0, sizeof(*lst));

  if ((flags & ~(FLEXTCP_LISTEN_REUSEPORT)) != 0) {
    fprintf(stderr, "flextcp_listen_open: unknown flags (%x)\n", flags);
    return -1;
  }

  if ((flags & FLEXTCP_LISTEN_REUSEPORT) == FLEXTCP_LISTEN_REUSEPORT) {
    f |= KERNEL_APPOUT_LISTEN_REUSEPORT;
  }

  kin += pos;

  if (kin->type != KERNEL_APPOUT_INVALID) {
    fprintf(stderr, "flextcp_listen_open: no queue space\n");
    return -1;
  }

  lst->conns = NULL;
  lst->local_port = port;
  lst->status = 0;

  kin->data.listen_open.opaque = OPAQUE(lst);
  kin->data.listen_open.local_port = port;
  kin->data.listen_open.backlog = backlog;
  kin->data.listen_open.flags = f;
  MEM_BARRIER();
  kin->type = KERNEL_APPOUT_LISTEN_OPEN;
  flextcp_kernel_kick();

  pos = pos + 1;
  if (pos >= ctx->kin_len) {
    pos = 0;
  }
  ctx->kin_head = pos;

  return 0;

}

int flextcp_listen_accept(struct flextcp_context *ctx,
    struct flextcp_listener *lst, struct flextcp_connection *conn)
{
  uint32_t pos = ctx->kin_head;
  struct kernel_appout *kin = ctx->kin_base;

  connection_init(conn);

  kin += pos;

  if (kin->type != KERNEL_APPOUT_INVALID) {
    fprintf(stderr, "flextcp_listen_accept: no queue space\n");
    return -1;
  }

  conn->status = CONN_ACCEPT_REQUESTED;
  conn->local_port = lst->local_port;

  kin->data.accept_conn.listen_opaque = OPAQUE(lst);
  kin->data.accept_conn.conn_opaque = OPAQUE(conn);
  kin->data.accept_conn.local_port = lst->local_port;
  MEM_BARRIER();
  kin->type = KERNEL_APPOUT_ACCEPT_CONN;
  flextcp_kernel_kick();

  pos = pos + 1;
  if (pos >= ctx->kin_len) {
    pos = 0;
  }
  ctx->kin_head = pos;

  return 0;
}

int flextcp_connection_open(struct flextcp_context *ctx,
    struct flextcp_connection *conn, uint32_t dst_ip, uint16_t dst_port)
{
  uint32_t pos = ctx->kin_head, f = 0;
  struct kernel_appout *kin = ctx->kin_base;

  connection_init(conn);

  kin += pos;

  if (kin->type != KERNEL_APPOUT_INVALID) {
    fprintf(stderr, "flextcp_connection_open: no queue space\n");
    return -1;
  }

  conn->status = CONN_OPEN_REQUESTED;
  conn->remote_ip = dst_ip;
  conn->remote_port = dst_port;

  kin->data.conn_open.opaque = OPAQUE(conn);
  kin->data.conn_open.remote_ip = dst_ip;
  kin->data.conn_open.remote_port = dst_port;
  kin->data.conn_open.flags = f;
  MEM_BARRIER();
  kin->type = KERNEL_APPOUT_CONN_OPEN;
  flextcp_kernel_kick();

  pos = pos + 1;
  if (pos >= ctx->kin_len) {
    pos = 0;
  }
  ctx->kin_head = pos;

  return 0;


}

int flextcp_connection_close(struct flextcp_context *ctx,
    struct flextcp_connection *conn)
{
  uint32_t pos = ctx->kin_head, f = 0;
  struct kernel_appout *kin = ctx->kin_base;
  struct flextcp_connection *p_c;

  /* need to remove connection from bump queue */
  if (conn->bump_pending != 0) {
    if (conn == ctx->bump_pending_first) {
      ctx->bump_pending_first = conn->bump_next;
    } else {
      for (p_c = ctx->bump_pending_first;
          p_c != NULL && p_c->bump_next != conn;
          p_c = p_c->bump_next);

      if (p_c == NULL) {
        fprintf(stderr, "connection_close: didn't find connection in "
            "bump list\n");
        abort();
      }

      p_c->bump_next = conn->bump_next;
      if (p_c->bump_next == NULL) {
        ctx->bump_pending_last = p_c;
      }
    }

    conn->bump_pending = 0;
  }

  kin += pos;

  if (kin->type != KERNEL_APPOUT_INVALID) {
    fprintf(stderr, "connection_close: no queue space\n");
    return -1;
  }

  /*if (reset)
    f |= KERNEL_APPOUT_CLOSE_RESET;*/

  conn->status = CONN_CLOSE_REQUESTED;

  kin->data.conn_close.opaque = (uintptr_t) conn;
  kin->data.conn_close.remote_ip = conn->remote_ip;
  kin->data.conn_close.remote_port = conn->remote_port;
  kin->data.conn_close.local_ip = conn->local_ip;
  kin->data.conn_close.local_port = conn->local_port;
  kin->data.conn_close.flags = f;
  MEM_BARRIER();
  kin->type = KERNEL_APPOUT_CONN_CLOSE;
  flextcp_kernel_kick();

  pos = pos + 1;
  if (pos >= ctx->kin_len) {
    pos = 0;
  }
  ctx->kin_head = pos;

  return 0;
}

int flextcp_connection_rx_done(struct flextcp_context *ctx,
    struct flextcp_connection *conn, size_t len)
{
  if (conn->rxb_used < len) {
    return -1;
  }

  conn->rxb_used -= len;

  /* Occasionally update the NIC on what we've already read. Force if buffer was
   * previously completely full*/
  conn->rxb_bump += len;
  if(conn->rxb_bump > conn->rxb_len / 4) {
    conn_mark_bump(ctx, conn);
  }

  return 0;
}

ssize_t flextcp_connection_tx_alloc(struct flextcp_connection *conn, size_t len,
    void **buf)
{
  uint32_t avail;
  uint32_t head;

  /* if outgoing connection has already been closed, abort */
  if ((conn->flags & CONN_FLAG_TXEOS) == CONN_FLAG_TXEOS)
    return -1;

  /* truncate if necessary */
  avail = conn_tx_allocbytes(conn);
  if (avail < len) {
    len = avail;
  }

  /* calculate alloc head */
  head = conn->txb_head + conn->txb_allocated;
  if (head >= conn->txb_len) {
    head -= conn->txb_len;
  }

  /* short alloc if we wrap around */
  if (head + len > conn->txb_len) {
    len = conn->txb_len - head;
  }

  *buf = conn->txb_base + head;

  /* bump head alloc counter */
  conn->txb_allocated += len;

  return len;
}

ssize_t flextcp_connection_tx_alloc2(struct flextcp_connection *conn, size_t len,
    void **buf_1, size_t *len_1, void **buf_2)
{
  uint32_t avail, head;

  /* if outgoing connection has already been closed, abort */
  if ((conn->flags & CONN_FLAG_TXEOS) == CONN_FLAG_TXEOS)
    return -1;

  /* truncate if necessary */
  avail = conn_tx_allocbytes(conn);
  if (avail < len) {
    len = avail;
  }

  /* calculate alloc head */
  head = conn->txb_head + conn->txb_allocated;
  if (head >= conn->txb_len) {
    head -= conn->txb_len;
  }

  *buf_1 = conn->txb_base + head;

  /* short alloc if we wrap around */
  if (head + len > conn->txb_len) {
    *len_1 = conn->txb_len - head;
    *buf_2 = conn->txb_base;
  } else {
    *len_1 = len;
    *buf_2 = NULL;
  }

  /* bump head alloc counter */
  conn->txb_allocated += len;
  return len;
}

int flextcp_connection_tx_send(struct flextcp_context *ctx,
    struct flextcp_connection *conn, size_t len)
{
  uint32_t next_head;

  if (conn_tx_sendbytes(conn) < len) {
    return -1;
  }

  conn->txb_allocated -= len;
  conn->txb_sent += len;

  next_head = conn->txb_head + len;
  if (next_head >= conn->txb_len) {
      next_head -= conn->txb_len;
  }
  conn->txb_head = next_head;

  conn->txb_bump += len;
  conn_mark_bump(ctx, conn);
  return 0;
}

int flextcp_connection_tx_close(struct flextcp_context *ctx,
        struct flextcp_connection *conn)
{
  /* if app hasn't sent all data yet, abort */
  if (conn_tx_sendbytes(conn) > 0) {
    fprintf(stderr, "flextcp_connection_tx_close: has unsent data\n");
    return -1;
  }

  /* if already closed, abort too */
  if ((conn->flags & CONN_FLAG_TXEOS) == CONN_FLAG_TXEOS) {
    fprintf(stderr, "flextcp_connection_tx_close: already closed\n");
    return -1;
  }

  conn->flags |= CONN_FLAG_TXEOS;

  /* try to push out to fastpath */
  flextcp_conn_pushtxeos(ctx, conn);

  return 0;
}

int flextcp_conn_pushtxeos(struct flextcp_context *ctx,
        struct flextcp_connection *conn)
{
  uint32_t head;
  assert(conn_tx_sendbytes(conn) == 0);
  assert((conn->flags & CONN_FLAG_TXEOS));

  /* if there is no tx buffer space we'll postpone until the next tx bump */
  if (conn_tx_allocbytes(conn) == 0) {
    return -1;
  }

  conn->txb_sent++;
  head = conn->txb_head + 1;
  if (head >= conn->txb_len) {
    head -= conn->txb_len;
  }
  conn->txb_head = head;

  conn->flags |= CONN_FLAG_TXEOS_ALLOC;

  conn->txb_bump++;
  conn_mark_bump(ctx, conn);
  return 0;
}

int flextcp_connection_tx_possible(struct flextcp_context *ctx,
    struct flextcp_connection *conn)
{
  return 0;
}

uint32_t flextcp_conn_txbuf_available(struct flextcp_connection *conn)
{
  return conn_tx_allocbytes(conn);
}

int flextcp_connection_move(struct flextcp_context *ctx,
        struct flextcp_connection *conn)
{
  uint32_t pos = ctx->kin_head;
  struct kernel_appout *kin = ctx->kin_base;

  kin += pos;

  if (kin->type != KERNEL_APPOUT_INVALID) {
    fprintf(stderr, "flextcp_connection_move: no queue space\n");
    return -1;
  }

  kin->data.conn_move.local_ip = conn->local_ip;
  kin->data.conn_move.remote_ip = conn->remote_ip;
  kin->data.conn_move.local_port = conn->local_port;
  kin->data.conn_move.remote_port = conn->remote_port;
  kin->data.conn_move.db_id = ctx->db_id;
  kin->data.conn_move.opaque = OPAQUE(conn);
  MEM_BARRIER();
  kin->type = KERNEL_APPOUT_CONN_MOVE;
  flextcp_kernel_kick();

  pos = pos + 1;
  if (pos >= ctx->kin_len) {
    pos = 0;
  }
  ctx->kin_head = pos;

  return 0;
}

static void connection_init(struct flextcp_connection *conn)
{
  memset(conn, 0, sizeof(*conn));
  conn->status = CONN_CLOSED;
}

static inline void conn_mark_bump(struct flextcp_context *ctx,
    struct flextcp_connection *conn)
{
  struct flextcp_connection *c_prev;

  if (conn->bump_pending) {
    return;
  }

  c_prev = ctx->bump_pending_last;
  conn->bump_next = NULL;
  conn->bump_prev = c_prev;
  if (c_prev != NULL) {
    c_prev->bump_next = conn;
  } else {
    ctx->bump_pending_first = conn;
  }
  ctx->bump_pending_last = conn;

  conn->bump_pending = 1;
}

/** Number of bytes in send buffer that can be allocated */
static inline uint32_t conn_tx_allocbytes(struct flextcp_connection *conn)
{
  return conn->txb_len - conn->txb_sent - conn->txb_allocated;
}

/** Number of bytes that have been allocated but not sent */
static inline uint32_t conn_tx_sendbytes(struct flextcp_connection *conn)
{
  return conn->txb_allocated;
}
