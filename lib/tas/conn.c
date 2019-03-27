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

static int listen_open(struct flextcp_context *ctx,
    struct flextcp_listener *lst, uint16_t port, uint32_t backlog,
    uint32_t flags, int obj, void *opptr);
static int listen_accept(struct flextcp_context *ctx,
    struct flextcp_listener *lst, struct flextcp_connection *conn,
    int obj, void *opptr_l, void *opptr_c);
static int connection_open(struct flextcp_context *ctx,
    struct flextcp_connection *conn, uint32_t dst_ip, uint16_t dst_port,
    uint32_t flags, int obj, void *opptr);
static int connection_close(struct flextcp_context *ctx,
    struct flextcp_connection *conn, int reset);
static void connection_init(struct flextcp_connection *conn);
static void oconn_init(struct flextcp_obj_connection *oconn);

static inline void conn_mark_bump(struct flextcp_context *ctx,
    struct flextcp_connection *conn);
static inline uint32_t conn_rx_recvdbytes(struct flextcp_connection *conn);
static inline uint32_t conn_tx_allocbytes(struct flextcp_connection *conn);
static inline uint32_t conn_tx_sendbytes(struct flextcp_connection *conn);

int flextcp_listen_open(struct flextcp_context *ctx,
    struct flextcp_listener *lst, uint16_t port, uint32_t backlog,
    uint32_t flags)
{
  return listen_open(ctx, lst, port, backlog, flags, 0, lst);
}

int flextcp_listen_accept(struct flextcp_context *ctx,
    struct flextcp_listener *lst, struct flextcp_connection *conn)
{
  connection_init(conn);
  return listen_accept(ctx, lst, conn, 0, lst, conn);
}

int flextcp_connection_open(struct flextcp_context *ctx,
    struct flextcp_connection *conn, uint32_t dst_ip, uint16_t dst_port)
{
  connection_init(conn);
  return connection_open(ctx, conn, dst_ip, dst_port, 0, 0, conn);
}

int flextcp_connection_close(struct flextcp_context *ctx,
    struct flextcp_connection *conn)
{
  if (conn->status != CONN_OPEN) {
    fprintf(stderr, "flextcp_connection_close: connection not open\n");
    return -1;
  }

  return connection_close(ctx, conn, 0);
}

int flextcp_connection_rx_done(struct flextcp_context *ctx,
    struct flextcp_connection *conn, size_t len)
{
  if (conn_rx_recvdbytes(conn) < len) {
    return -1;
  }

  conn->rxb_tail += len;
  if (conn->rxb_tail >= conn->rxb_len) {
    conn->rxb_tail -= conn->rxb_len;
  }

  /* Occasionally update the NIC on what we've already read */
  if((conn->rxb_head - conn->rxb_nictail) % conn->rxb_len >= conn->rxb_len / 4) {
    conn_mark_bump(ctx, conn);
  }

  return 0;
}

ssize_t flextcp_connection_tx_alloc(struct flextcp_connection *conn, size_t len,
    void **buf)
{
  uint32_t avail;

  /* if outgoing connection has already been closed, abort */
  if ((conn->flags & CONN_FLAG_TXEOS) == CONN_FLAG_TXEOS)
    return -1;

  /* truncate if necessary */
  avail = conn_tx_allocbytes(conn);
  if (avail < len) {
    len = avail;
  }

  /* short alloc if we wrap around */
  if (conn->txb_head_alloc + len > conn->txb_len) {
    len = conn->txb_len - conn->txb_head_alloc;
  }

  *buf = conn->txb_base + conn->txb_head_alloc;

  /* bump head alloc pointer */
  conn->txb_head_alloc += len;
  if (conn->txb_head_alloc >= conn->txb_len) {
    conn->txb_head_alloc -= conn->txb_len;
  }

  return len;
}

ssize_t flextcp_connection_tx_alloc2(struct flextcp_connection *conn, size_t len,
    void **buf_1, size_t *len_1, void **buf_2)
{
  uint32_t avail;

  /* if outgoing connection has already been closed, abort */
  if ((conn->flags & CONN_FLAG_TXEOS) == CONN_FLAG_TXEOS)
    return -1;

  /* truncate if necessary */
  avail = conn_tx_allocbytes(conn);
  if (avail < len) {
    len = avail;
  }

  *buf_1 = conn->txb_base + conn->txb_head_alloc;

  /* short alloc if we wrap around */
  if (conn->txb_head_alloc + len > conn->txb_len) {
    *len_1 = conn->txb_len - conn->txb_head_alloc;
    *buf_2 = conn->txb_base;
  } else {
    *len_1 = len;
    *buf_2 = NULL;
  }


  /* bump head alloc pointer */
  conn->txb_head_alloc += len;
  if (conn->txb_head_alloc >= conn->txb_len) {
    conn->txb_head_alloc -= conn->txb_len;
  }

  return len;
}

int flextcp_connection_tx_send(struct flextcp_context *ctx,
    struct flextcp_connection *conn, size_t len)
{
  uint32_t next_head;

  if (conn_tx_sendbytes(conn) < len) {
    return -1;
  }

  next_head = conn->txb_head + len;
  if (next_head >= conn->txb_len) {
      next_head -= conn->txb_len;
  }
  conn->txb_head = next_head;

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
  assert(conn_tx_sendbytes(conn) == 0);
  assert((conn->flags & CONN_FLAG_TXEOS));

  /* if there is no tx buffer space we'll postpone until the next tx bump */
  if (conn_tx_allocbytes(conn) == 0) {
    return -1;
  }

  conn->txb_head_alloc++;
  if (conn->txb_head_alloc >= conn->txb_len) {
    conn->txb_head_alloc -= conn->txb_len;
  }

  conn->txb_head = conn->txb_head_alloc;

  conn->flags |= CONN_FLAG_TXEOS_ALLOC;

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
  kin->data.conn_move.opaque = OPAQUE(conn, 0);
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

int flextcp_obj_listen_open(struct flextcp_context *ctx,
    struct flextcp_obj_listener *lst, uint16_t port, uint32_t backlog,
    uint32_t flags)
{
  return listen_open(ctx, &lst->l, port, backlog, flags, 1, lst);
}

int flextcp_obj_listen_accept(struct flextcp_context *ctx,
    struct flextcp_obj_listener *lst, struct flextcp_obj_connection *conn)
{
  oconn_init(conn);
  return listen_accept(ctx, &lst->l, &conn->c, 1, lst, conn);
}

int flextcp_obj_connection_open(struct flextcp_context *ctx,
    struct flextcp_obj_connection *conn, uint32_t dst_ip, uint16_t dst_port,
    uint32_t flags)
{
  oconn_init(conn);
  return connection_open(ctx, &conn->c, dst_ip, dst_port, flags, 1, conn);
}

void flextcp_obj_connection_rx_done(struct flextcp_context *ctx,
    struct flextcp_obj_connection *oconn, struct flextcp_obj_handle *oh)
{
  struct flextcp_connection *conn = &oconn->c;
  struct obj_hdr ohdr;
  uint32_t pos;

  /* Can be in one of two cases:
   *  - This is the first non-freed object: i.e. rx_tail == object pos
   *    + repeatedly check next object until we find last object or a
   *      non-freed object, then bump tail
   *  - There is at least one non-freed object in front of this object
   *    + only flag this object as done
   */

  oconn_lock(oconn);

  /* read object header */
  pos = oh->pos;
  circ_read(&ohdr, conn->rxb_base, conn->rxb_len, pos, sizeof(ohdr));

  if (pos == conn->rxb_tail) {
    /* Case 1: check following objects */

    do {
      pos = circ_offset(pos, conn->rxb_len,
          sizeof(ohdr) + ohdr.dstlen + f_beui32(ohdr.len));

      if (pos == conn->rxb_head) {
        /* reached last allocated object */
        break;
      }

      /* read next header */
      circ_read(&ohdr, conn->rxb_base, conn->rxb_len, pos, sizeof(ohdr));
    } while ((f_beui16(ohdr.magic) & OBJ_FLAG_DONE) != 0);

    conn->rxb_tail = pos;
  } else {
    /* Case 2: mark object as done */
    ohdr.magic = t_beui16(OBJ_FLAG_DONE);
    circ_write(&ohdr, conn->rxb_base, conn->rxb_len, pos, sizeof(ohdr));
  }

  oconn_unlock(oconn);
}

int flextcp_obj_connection_tx_alloc(struct flextcp_obj_connection *oconn,
    uint8_t dstlen, size_t len, void **buf1, size_t *len1, void **buf2,
    struct flextcp_obj_handle *oh)
{
  struct flextcp_connection *conn = &oconn->c;
  struct obj_hdr ohdr;
  uint32_t avail, total, pos;

  total = sizeof(ohdr) + dstlen + len;

  oconn_lock(oconn);

  /* Check if there is enough space */
  avail = conn_tx_allocbytes(conn);
  if (avail < total) {
    oconn_unlock(oconn);
    return -1;
  }

  /* Initialize object handle */
  oh->pos = conn->txb_head_alloc;

  /* Prepare and write object header */
  ohdr.len = t_beui32(len);
  ohdr.magic = t_beui16(0);
  ohdr.src = 0;
  ohdr.dstlen = dstlen;
  circ_write(&ohdr, conn->txb_base, conn->txb_len, conn->txb_head_alloc,
      sizeof(ohdr));

  /* Skip header for returned range */
  pos = circ_offset(conn->txb_head_alloc, conn->txb_len, sizeof(ohdr));

  /* Return one or two buffers, depending on whether there is a wrap around */
  circ_range(buf1, len1, buf2, conn->txb_base, conn->txb_len, pos,
      dstlen + len);

  /* Bump head alloc pointer */
  conn->txb_head_alloc = circ_offset(conn->txb_head_alloc, conn->txb_len,
      total);

  oconn_unlock(oconn);

  return 0;
}

void flextcp_obj_connection_tx_send(struct flextcp_context *ctx,
        struct flextcp_obj_connection *oconn, struct flextcp_obj_handle *oh)
{
  struct flextcp_connection *conn = &oconn->c;
  struct obj_hdr ohdr;
  uint32_t pos;

  /* Can be in one of two cases:
   *  1) This is the first non-sent object: i.e. tx_head == object pos
   *     + repeatedly check next object until we find last object or a
   *       non-ready object, then bump tail
   *  2) There is at least one non-ready object in front of this object
   *     + only flag this object as ready
   */

  oconn_lock(oconn);

  /* read object header */
  pos = oh->pos;
  circ_read(&ohdr, conn->txb_base, conn->txb_len, pos, sizeof(ohdr));

  if (pos == conn->txb_head) {
    /* Case 1: check following objects */

    do {
      /* Write in magic number to replace flags */
      ohdr.magic = t_beui16(OBJ_MAGIC);
      circ_write(&ohdr, conn->txb_base, conn->txb_len, pos, sizeof(ohdr));

      pos = circ_offset(pos, conn->txb_len,
          sizeof(ohdr) + ohdr.dstlen + f_beui32(ohdr.len));

      if (pos == conn->txb_head_alloc) {
        /* reached last allocated object */
        break;
      }

      /* read next header */
      circ_read(&ohdr, conn->txb_base, conn->txb_len, pos, sizeof(ohdr));
    } while ((f_beui16(ohdr.magic) & OBJ_FLAG_DONE) != 0);

    conn->txb_head = pos;
  } else {
    /* Case 2: mark object as done */
    ohdr.magic = t_beui16(OBJ_FLAG_DONE);
    circ_write(&ohdr, conn->txb_base, conn->txb_len, pos, sizeof(ohdr));
  }

  oconn_unlock(oconn);
}

int flextcp_obj_connection_bump(struct flextcp_context *ctx,
        struct flextcp_obj_connection *oconn)
{
  struct flextcp_connection *conn = &oconn->c;
  int ret = 0;
  struct flextcp_pl_atx *atx;

  oconn_lock(oconn);

  if (conn->txb_head == conn->txb_nichead &&
      ((conn->rxb_head - conn->rxb_nictail) % conn->rxb_len <
       conn->rxb_len / 2))
  {
    /* no bumping required */
    goto out;
  }

  if (flextcp_context_tx_alloc(ctx, &atx, conn->fn_core) != 0) {
    ret = -1;
    goto out;
  }

  /* fill in tx queue entry */
  atx->msg.connupdate.rx_tail = conn->rxb_tail;
  atx->msg.connupdate.tx_head = conn->txb_head;
  atx->msg.connupdate.flow_id = conn->flow_id;
  atx->msg.connupdate.bump_seq = conn->bump_seq++;
  MEM_BARRIER();
  atx->type = FLEXTCP_PL_ATX_CONNUPDATE;

  flextcp_context_tx_done(ctx, conn->fn_core);

  conn->rxb_nictail = conn->rxb_tail;
  conn->txb_nichead = conn->txb_head;
out:
  oconn_unlock(oconn);
  return ret;
}


static int listen_open(struct flextcp_context *ctx,
    struct flextcp_listener *lst, uint16_t port, uint32_t backlog,
    uint32_t flags, int obj, void *opptr)
{
  uint32_t pos = ctx->kin_head;
  struct kernel_appout *kin = ctx->kin_base;
  uint32_t f = 0;

  if ((flags & ~(FLEXTCP_LISTEN_REUSEPORT)) != 0) {
    fprintf(stderr, "flextcp_listen_open: unknown flags (%x)\n", flags);
    return -1;
  }

  if ((flags & FLEXTCP_LISTEN_REUSEPORT) == FLEXTCP_LISTEN_REUSEPORT) {
    f |= KERNEL_APPOUT_LISTEN_REUSEPORT;
  }
  if (obj) {
    f |= KERNEL_APPOUT_LISTEN_OBJSOCK;
    if ((flags & FLEXTCP_LISTEN_OBJNOHASH) == FLEXTCP_LISTEN_OBJNOHASH) {
      f |= KERNEL_APPOUT_LISTEN_OBJNOHASH;
    }
  }

  kin += pos;

  if (kin->type != KERNEL_APPOUT_INVALID) {
    fprintf(stderr, "flextcp_listen_open: no queue space\n");
    return -1;
  }

  lst->conns = NULL;
  lst->local_port = port;
  lst->status = 0;

  kin->data.listen_open.opaque = OPAQUE(opptr, obj);
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

static int listen_accept(struct flextcp_context *ctx,
    struct flextcp_listener *lst, struct flextcp_connection *conn,
    int obj, void *opptr_l, void *opptr_c)
{
  uint32_t pos = ctx->kin_head;
  struct kernel_appout *kin = ctx->kin_base;

  kin += pos;

  if (kin->type != KERNEL_APPOUT_INVALID) {
    fprintf(stderr, "flextcp_listen_accept: no queue space\n");
    return -1;
  }

  conn->status = CONN_ACCEPT_REQUESTED;
  conn->local_port = lst->local_port;

  kin->data.accept_conn.listen_opaque = OPAQUE(opptr_l, obj);
  kin->data.accept_conn.conn_opaque = OPAQUE(opptr_c, obj);
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

static int connection_open(struct flextcp_context *ctx,
    struct flextcp_connection *conn, uint32_t dst_ip, uint16_t dst_port,
    uint32_t flags, int obj, void *opptr)
{
  uint32_t pos = ctx->kin_head, f = 0;
  struct kernel_appout *kin = ctx->kin_base;

  kin += pos;

  if (kin->type != KERNEL_APPOUT_INVALID) {
    fprintf(stderr, "flextcp_connection_open: no queue space\n");
    return -1;
  }

  if (obj) {
    f |= KERNEL_APPOUT_OPEN_OBJSOCK;
    if ((flags & FLEXTCP_CONNECT_OBJNOHASH) == FLEXTCP_CONNECT_OBJNOHASH) {
      f |= KERNEL_APPOUT_OPEN_OBJNOHASH;
    }
  }

  conn->status = CONN_OPEN_REQUESTED;
  conn->remote_ip = dst_ip;
  conn->remote_port = dst_port;

  kin->data.conn_open.opaque = OPAQUE(opptr, obj);
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

static int connection_close(struct flextcp_context *ctx,
    struct flextcp_connection *conn, int reset)
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

  if (reset)
    f |= KERNEL_APPOUT_CLOSE_RESET;

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

static void connection_init(struct flextcp_connection *conn)
{
  conn->rxb_head = 0;
  conn->rxb_tail = 0;
  conn->rxb_nictail = 0;
  conn->txb_head = 0;
  conn->txb_head_alloc = 0;
  conn->txb_tail = 0;
  conn->txb_nichead = 0;
  conn->bump_seq = 0;
  conn->status = CONN_CLOSED;
  conn->flags = 0;
  conn->rx_closed = 0;
}

static void oconn_init(struct flextcp_obj_connection *oconn)
{
  memset(oconn, 0, sizeof(*oconn));
  connection_init(&oconn->c);
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

/** Number of bytes in receive buffer that have been received */
static inline uint32_t conn_rx_recvdbytes(struct flextcp_connection *conn)
{
  if (conn->rxb_tail <= conn->rxb_head) {
    return conn->rxb_head - conn->rxb_tail;
  } else {
    return conn->rxb_len - conn->rxb_tail + conn->rxb_head;
  }
}

/** Number of bytes in send buffer that can be allocated */
static inline uint32_t conn_tx_allocbytes(struct flextcp_connection *conn)
{
  if (conn->txb_tail <= conn->txb_head_alloc) {
    return conn->txb_len - conn->txb_head_alloc + conn->txb_tail - 1;
  } else {
    return conn->txb_tail - conn->txb_head_alloc - 1;
  }
}

/** Number of bytes that have been allocated but not sent */
static inline uint32_t conn_tx_sendbytes(struct flextcp_connection *conn)
{
  if (conn->txb_head <= conn->txb_head_alloc) {
    return conn->txb_head_alloc - conn->txb_head;
  } else {
    return conn->txb_len - conn->txb_head + conn->txb_head_alloc;
  }
}
