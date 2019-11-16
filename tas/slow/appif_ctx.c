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

/**
 * @brief Handling application context kernel queues.
 * @file appif_ctx.c
 * @addtogroup kernel-appif
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <tas.h>
#include <utils_log.h>
#include <slowpath.h>
#include "internal.h"
#include "appif.h"

static int kin_conn_open(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout);
static int kin_conn_move(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout);
static int kin_conn_close(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout);
static int kin_listen_open(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout);
static int kin_accept_conn(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout);
static int kin_req_scale(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout);
extern struct connection *conn_ht_lookup(uint64_t opaque, uint32_t local_ip,
           uint32_t remote_ip, uint16_t local_port, uint16_t remote_port);


#ifdef QUEUE_STATS
void appqueue_stats_dump()
{
  TAS_LOG(INFO, MAIN, "app -> slow stats: cyc=%lu count=%lu avg_queuing_delay=%lf\n",
          STATS_FETCH(slowpath_ctx, appin_cycles),
          STATS_FETCH(slowpath_ctx, appin_count),
          (double) STATS_FETCH(slowpath_ctx, appin_cycles)/ STATS_FETCH(slowpath_ctx, appin_count));
}
#endif

static void appif_ctx_kick(struct app_context *ctx)
{
  assert(ctx->evfd != 0);
  uint32_t now = util_timeout_time_us();

  /* fprintf(stderr, "kicking app context?\n"); */

  if(now - ctx->last_ts > POLL_CYCLE) {
    uint64_t val = 1;
    /* fprintf(stderr, "kicking app context!\n"); */
    int r = write(ctx->evfd, &val, sizeof(uint64_t));
    assert(r == sizeof(uint64_t));
  }

  ctx->last_ts = now;
}

void appif_conn_opened(struct connection *c, int status)
{
  struct app_context *ctx = c->ctx;
  volatile struct kernel_appin *kout = ctx->kout_base;
  uint32_t kout_pos = ctx->kout_pos;

  kout += kout_pos;

  /* make sure we have room for a response */
  if (kout->type != KERNEL_APPIN_INVALID) {
    fprintf(stderr, "appif_conn_opened: No space in kout queue (TODO)\n");
    return;
  }

  kout->data.conn_opened.opaque = c->opaque;
  kout->data.conn_opened.status = status;
  if (status == 0) {
    kout->data.conn_opened.rx_off = c->rx_buf - (uint8_t *) tas_shm;
    kout->data.conn_opened.tx_off = c->tx_buf - (uint8_t *) tas_shm;
    kout->data.conn_opened.rx_len = c->rx_len;
    kout->data.conn_opened.tx_len = c->tx_len;

    kout->data.conn_opened.seq_rx = c->remote_seq;
    kout->data.conn_opened.seq_tx = c->local_seq;
    kout->data.conn_opened.local_ip = config.ip;
    kout->data.conn_opened.local_port = c->local_port;
    kout->data.conn_opened.flow_id = c->flow_id;
    kout->data.conn_opened.fn_core = c->fn_core;
  } else {
    /* remove from app connection list */
    struct application *app = ctx->app;
    if (app->conns == c) {
      app->conns = c->app_next;
    } else {
      struct connection *c_crwl;
      for (c_crwl = app->conns; c_crwl != NULL && c_crwl->app_next != c;
        c_crwl = c_crwl->app_next);
      if (c_crwl == NULL) {
        fprintf(stderr, "appif_conn_closed: connection not found\n");
        abort();
      }
      c_crwl->app_next = c->app_next;
    }
    tcp_destroy(c);
  }

  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_CONN_OPENED;
  appif_ctx_kick(ctx);

  kout_pos++;
  if (kout_pos >= ctx->kout_len) {
    kout_pos = 0;
  }
  ctx->kout_pos = kout_pos;
}

void appif_conn_closed(struct connection *c, int status)
{
  struct app_context *ctx = c->ctx;
  struct application *app = ctx->app;
  volatile struct kernel_appin *kout = ctx->kout_base;
  uint32_t kout_pos = ctx->kout_pos;

  kout += kout_pos;

  /* make sure we have room for a response */
  if (kout->type != KERNEL_APPIN_INVALID) {
    fprintf(stderr, "appif_conn_closed: No space in kout queue (TODO)\n");
    return;
  }

  kout->data.status.opaque = c->opaque;
  kout->data.status.status = status;

  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_STATUS_CONN_CLOSE;
  appif_ctx_kick(ctx);

  kout_pos++;
  if (kout_pos >= ctx->kout_len) {
    kout_pos = 0;
  }
  ctx->kout_pos = kout_pos;

  /* remove from app connection list */
  if (app->conns == c) {
    app->conns = c->app_next;
    c->app_prev = NULL;
  } else {
    struct connection* c_prev = c->app_prev;
    struct connection* c_next = c->app_next;

    if (c_prev != NULL)
      c_prev->app_next = c->app_next;
    if (c_next != NULL)
      c_next->app_prev = c->app_prev;
  }
}

void appif_listen_newconn(struct listener *l, uint32_t remote_ip,
    uint16_t remote_port)
{
  struct app_context *ctx = l->ctx;
  volatile struct kernel_appin *kout = ctx->kout_base;
  uint32_t kout_pos = ctx->kout_pos;

  kout += kout_pos;

  /* make sure we have room for a response */
  if (kout->type != KERNEL_APPIN_INVALID) {
    fprintf(stderr, "appif_listen_newconn: No space in kout queue (TODO)\n");
    return;
  }

  kout->data.listen_newconn.opaque = l->opaque;
  kout->data.listen_newconn.remote_ip = remote_ip;
  kout->data.listen_newconn.remote_port = remote_port;
  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_LISTEN_NEWCONN;
  appif_ctx_kick(ctx);

  kout_pos++;
  if (kout_pos >= ctx->kout_len) {
    kout_pos = 0;
  }
  ctx->kout_pos = kout_pos;

}

void appif_accept_conn(struct connection *c, int status)
{
  struct app_context *ctx = c->ctx;
  struct application *app = ctx->app;
  volatile struct kernel_appin *kout = ctx->kout_base;
  uint32_t kout_pos = ctx->kout_pos;

  kout += kout_pos;

  /* make sure we have room for a response */
  if (kout->type != KERNEL_APPIN_INVALID) {
    fprintf(stderr, "appif_accept_conn: No space in kout queue (TODO)\n");
    return;
  }

  kout->data.accept_connection.opaque = c->opaque;
  kout->data.accept_connection.status = status;
  if (status == 0) {
    kout->data.accept_connection.rx_off = c->rx_buf - (uint8_t *) tas_shm;
    kout->data.accept_connection.tx_off = c->tx_buf - (uint8_t *) tas_shm;
    kout->data.accept_connection.rx_len = c->rx_len;
    kout->data.accept_connection.tx_len = c->tx_len;

    kout->data.accept_connection.seq_rx = c->remote_seq;
    kout->data.accept_connection.seq_tx = c->local_seq;
    kout->data.accept_connection.local_ip = config.ip;
    kout->data.accept_connection.remote_ip = c->remote_ip;
    kout->data.accept_connection.remote_port = c->remote_port;
    kout->data.accept_connection.flow_id = c->flow_id;
    kout->data.accept_connection.fn_core = c->fn_core;

    c->app_next = app->conns;
    c->app_prev = NULL;
    if (app->conns != NULL)
      app->conns->app_prev = c;
    app->conns = c;
  } else {
    tcp_destroy(c);
  }

  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_ACCEPTED_CONN;
  appif_ctx_kick(ctx);

  kout_pos++;
  if (kout_pos >= ctx->kout_len) {
    kout_pos = 0;
  }
  ctx->kout_pos = kout_pos;
}


unsigned appif_ctx_poll(struct application *app, struct app_context *ctx)
{
  STATS_TS(start);
  volatile struct kernel_appout *kin = ctx->kin_base;
  volatile struct kernel_appin *kout = ctx->kout_base;
  uint32_t kin_pos = ctx->kin_pos;
  uint32_t kout_pos = ctx->kout_pos;
  uint8_t type;
  int kout_inc = 0;

  kin += kin_pos;
  kout += kout_pos;

  /* make sure we have room for a response */
  if (kout->type != KERNEL_APPIN_INVALID) {
    return 0;
  }

  type = kin->type;
  MEM_BARRIER();

#ifdef QUEUE_STATS
  if (type != KERNEL_APPOUT_INVALID)
  {
    STATS_ADD(slowpath_ctx, appin_cycles, (util_rdtsc() - kin->ts));
    STATS_ADD(slowpath_ctx, appin_count, 1);
  }
#endif

  switch (type) {
    case KERNEL_APPOUT_INVALID:
      /* nothing yet */
      return 0;

    case KERNEL_APPOUT_CONN_OPEN:
      /* connection request */
      kout_inc += kin_conn_open(app, ctx, kin, kout);
      break;

    case KERNEL_APPOUT_CONN_MOVE:
      /* connection move request */
      kout_inc += kin_conn_move(app, ctx, kin, kout);
      STATS_TS(end_move);
      STATS_ADD(slowpath_ctx, cyc_kmove, end_move-start);
      break;

    case KERNEL_APPOUT_CONN_CLOSE:
      /* connection close request */
      kout_inc += kin_conn_close(app, ctx, kin, kout);
      STATS_TS(end_close);
      STATS_ADD(slowpath_ctx, cyc_kclose, end_close-start);
      break;

    case KERNEL_APPOUT_LISTEN_OPEN:
      /* listen request */
      kout_inc += kin_listen_open(app, ctx, kin, kout);
      STATS_TS(end_lopen);
      STATS_ADD(slowpath_ctx, cyc_klopen, end_lopen-start);
      break;

    case KERNEL_APPOUT_ACCEPT_CONN:
      /* accept request */
      (void) app;
      kout_inc += kin_accept_conn(app, ctx, kin, kout);
      STATS_TS(end);
      STATS_ADD(slowpath_ctx, cyc_kac, end-start);
      break;

    case KERNEL_APPOUT_REQ_SCALE:
      /* scaling request */
      kout_inc += kin_req_scale(app, ctx, kin, kout);
      STATS_TS(end_req_scale);
      STATS_ADD(slowpath_ctx, cyc_kreq_scale, end_req_scale-start);
      break;

    case KERNEL_APPOUT_LISTEN_CLOSE:
    default:
      fprintf(stderr, "kin_poll: unsupported request type %u\n", kin->type);
      break;
  }

  MEM_BARRIER();
  kin->type = 0;

  /* update kin queue position */
  kin_pos++;
  if (kin_pos >= ctx->kin_len) {
    kin_pos = 0;
  }
  ctx->kin_pos = kin_pos;

  /* update kout queue position if the entry was used */
  if (kout_inc > 0) {
    kout_pos += kout_inc;
    if (kout_pos >= ctx->kout_len) {
      kout_pos = 0;
    }
    ctx->kout_pos = kout_pos;
  }

  return 1;
}

static int kin_conn_open(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout)
{
  struct connection *conn;

  if (tcp_open(ctx, kin->data.conn_open.opaque, kin->data.conn_open.remote_ip,
      kin->data.conn_open.remote_port, ctx->doorbell->id, &conn) != 0)
  {
    fprintf(stderr, "kin_conn_open: tcp_open failed\n");
    goto error;
  }

  conn->app_next = app->conns;
  conn->app_prev = NULL;
  if (app->conns != NULL)
    app->conns->app_prev = conn;
  app->conns = conn;

  return 0;

error:
  kout->data.conn_opened.opaque = kin->data.conn_open.opaque;
  kout->data.conn_opened.status = -1;
  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_CONN_OPENED;
  appif_ctx_kick(ctx);
  return 1;
}

static int kin_conn_move(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout)
{
  struct connection *conn;
  struct app_context *new_ctx;

  conn = conn_ht_lookup(kin->data.conn_move.opaque,
                    kin->data.conn_move.local_ip,
                    kin->data.conn_move.remote_ip,
                    kin->data.conn_move.local_port,
                    kin->data.conn_move.remote_port);

  if (conn == NULL) {
    fprintf(stderr, "kin_conn_move: connection not found\n");
    goto error;
  }

  for (new_ctx = app->contexts; new_ctx != NULL; new_ctx = new_ctx->next) {
    if (new_ctx->doorbell->id == kin->data.conn_move.db_id) {
      break;
    }
  }
  if (new_ctx == NULL) {
    fprintf(stderr, "kin_conn_move: destination context not found\n");
    goto error;
  }

  if (conn->status != CONN_OPEN) {
    fprintf(stderr, "kin_conn_move: connection not open\n");
    goto error;
  }

  if (nicif_connection_move(new_ctx->doorbell->id, conn->flow_id) != 0) {
    fprintf(stderr, "kin_conn_move: nicif_connection_move failed\n");
    goto error;
  }

  kout->data.status.opaque = kin->data.conn_move.opaque;
  kout->data.status.status = 0;
  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_STATUS_CONN_MOVE;
  appif_ctx_kick(ctx);
  return 1;

error:
  kout->data.status.opaque = kin->data.conn_move.opaque;
  kout->data.status.status = -1;
  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_STATUS_CONN_MOVE;
  appif_ctx_kick(ctx);
  return 1;
}

static int kin_conn_close(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout)
{
  struct connection *conn;

  STATS_TS(conn_close_iter_start);
  conn = conn_ht_lookup(kin->data.conn_move.opaque,
                    kin->data.conn_move.local_ip,
                    kin->data.conn_move.remote_ip,
                    kin->data.conn_move.local_port,
                    kin->data.conn_move.remote_port);
  STATS_TS(conn_close_iter_end);
  STATS_ADD(slowpath_ctx, cyc_conn_close_iter, conn_close_iter_end - conn_close_iter_start);
  STATS_ADD(slowpath_ctx, conn_close_cnt, 1);

  if (conn == NULL) {
    fprintf(stderr, "kin_conn_close: connection not found\n");
    goto error;
  }

  if (tcp_close(conn) != 0) {
    fprintf(stderr, "kin_conn_close: tcp_close failed\n");
    goto error;
  }

  return 0;

error:
  fprintf(stderr, "Error in kin_conn_close\n");
  kout->data.status.opaque = kin->data.conn_close.opaque;
  kout->data.status.status = -1;
  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_STATUS_CONN_CLOSE;
  appif_ctx_kick(ctx);
  return 1;
}

static int kin_listen_open(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout)
{
  struct listener *listen;

  if (tcp_listen(ctx, kin->data.listen_open.opaque,
        kin->data.listen_open.local_port, kin->data.listen_open.backlog,
        !!(kin->data.listen_open.flags & KERNEL_APPOUT_LISTEN_REUSEPORT),
        &listen) != 0)
  {
    fprintf(stderr, "kin_listen_open: tcp_listen failed\n");
    goto error;
  }

  listen->app_next = app->listeners;
  app->listeners = listen;

  kout->data.status.opaque = kin->data.listen_open.opaque;
  kout->data.status.status = 0;
  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_STATUS_LISTEN_OPEN;
  appif_ctx_kick(ctx);

  return 1;

error:
  kout->data.status.opaque = kin->data.listen_open.opaque;
  kout->data.status.status = -1;
  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_STATUS_LISTEN_OPEN;
  appif_ctx_kick(ctx);
  return 1;
}

static int kin_accept_conn(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout)
{
  struct listener *listen;

  /* look for listen struct */
  for (listen = app->listeners; listen != NULL; listen = listen->app_next) {
    if (listen->port == kin->data.accept_conn.local_port &&
        listen->opaque == kin->data.accept_conn.listen_opaque)
    {
      break;
    }
  }
  if (tcp_accept(ctx, kin->data.accept_conn.conn_opaque, listen,
        ctx->doorbell->id) != 0)
  {
    fprintf(stderr, "kin_accept_conn\n");
    goto error;
  }

  return 0;

error:
  kout->data.accept_connection.opaque = kin->data.accept_conn.conn_opaque;
  kout->data.accept_connection.status = -1;
  MEM_BARRIER();
  kout->ts = util_rdtsc();
  kout->type = KERNEL_APPIN_ACCEPTED_CONN;
  kout->ts = util_rdtsc();
  appif_ctx_kick(ctx);
  return 1;
}

extern int flexnic_scale_to(uint32_t cores);

static int kin_req_scale(struct application *app, struct app_context *ctx,
    volatile struct kernel_appout *kin, volatile struct kernel_appin *kout)
{
  uint32_t num_cores = kin->data.req_scale.num_cores;

  flexnic_scale_to(num_cores);

  return 0;
}
