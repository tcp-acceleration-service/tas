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
#include <signal.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include <tas_memif.h>

#include "internal.h"
#include "fastemu.h"

#define DATAPLANE_TSCS

#ifdef DATAPLANE_STATS
# ifdef DATAPLANE_TSCS
#   define STATS_TS(n) uint64_t n = rte_get_tsc_cycles()
#   define STATS_TSADD(c, f, n) __sync_fetch_and_add(&c->stat_##f, n)
# else
#   define STATS_TS(n) do { } while (0)
#   define STATS_TSADD(c, f, n) do { } while (0)
# endif
#   define STATS_ADD(c, f, n) __sync_fetch_and_add(&c->stat_##f, n)
#else
#   define STATS_TS(n) do { } while (0)
#   define STATS_TSADD(c, f, n) do { } while (0)
#   define STATS_ADD(c, f, n) do { } while (0)
#endif

static void dataplane_block(struct dataplane_context *ctx, uint32_t ts);
static unsigned poll_rx(struct dataplane_context *ctx, uint32_t ts,
    uint64_t tsc) __attribute__((noinline));
static unsigned poll_queues(struct dataplane_context *ctx, uint32_t ts)  __attribute__((noinline));
static unsigned poll_active_queues(struct dataplane_context *ctx, uint32_t ts);
static unsigned poll_all_queues(struct dataplane_context *ctx, uint32_t ts);
static unsigned poll_kernel(struct dataplane_context *ctx, uint32_t ts) __attribute__((noinline));
static unsigned poll_qman(struct dataplane_context *ctx, uint32_t ts) __attribute__((noinline));
static unsigned poll_qman_fwd(struct dataplane_context *ctx, uint32_t ts) __attribute__((noinline));
static void poll_scale(struct dataplane_context *ctx);

static void enqueue_ctx_to_active(struct polled_app *act_app, uint32_t cid);
static void remove_ctx_from_active(struct polled_app *act_app, 
    struct polled_context *act_ctx);
static void enqueue_app_to_active(struct dataplane_context *ctx, uint16_t aid);
static void remove_app_from_active(struct dataplane_context *ctx, 
    struct polled_app *act_app);

static void polled_app_init(struct polled_app *app, uint16_t id);
static void polled_ctx_init(struct polled_context *ctx, uint32_t id);

static inline uint8_t bufcache_prealloc(struct dataplane_context *ctx, uint16_t num,
    struct network_buf_handle ***handles);
static inline void bufcache_alloc(struct dataplane_context *ctx, uint16_t num);
static inline void bufcache_free(struct dataplane_context *ctx,
    struct network_buf_handle *handle);

static inline void tx_flush(struct dataplane_context *ctx);
static inline void tx_send(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, uint16_t off, uint16_t len);

static void arx_cache_flush(struct dataplane_context *ctx, uint64_t tsc) __attribute__((noinline));

int dataplane_init(void)
{
  if (FLEXNIC_INTERNAL_MEM_SIZE < sizeof(struct flextcp_pl_mem)) {
    fprintf(stderr, "dataplane_init: internal flexnic memory size not "
        "sufficient (got %x, need %zx)\n", FLEXNIC_INTERNAL_MEM_SIZE,
        sizeof(struct flextcp_pl_mem));
    return -1;
  }

  if (fp_cores_max > FLEXNIC_PL_APPST_CTX_MCS) {
    fprintf(stderr, "dataplane_init: more cores than FLEXNIC_PL_APPST_CTX_MCS "
        "(%u)\n", FLEXNIC_PL_APPST_CTX_MCS);
    return -1;
  }
  if (FLEXNIC_PL_FLOWST_NUM > FLEXNIC_NUM_QMQUEUES) {
    fprintf(stderr, "dataplane_init: more flow states than queue manager queues"
        "(%u > %u)\n", FLEXNIC_PL_FLOWST_NUM, FLEXNIC_NUM_QMQUEUES);
    return -1;
  }

  return 0;
}

int dataplane_context_init(struct dataplane_context *ctx)
{
  int i, j;
  char name[32];
  struct polled_app *p_app;
  struct polled_context *p_ctx;

  /* initialize forwarding queue */
  sprintf(name, "qman_fwd_ring_%u", ctx->id);
  if ((ctx->qman_fwd_ring = rte_ring_create(name, 32 * 1024, rte_socket_id(),
          RING_F_SC_DEQ)) == NULL)
  {
    fprintf(stderr, "initializing rte_ring_create\n");
    return -1;
  }

  /* initialize queue manager */
  if (qman_thread_init(ctx) != 0) {
    fprintf(stderr, "initializing qman thread failed\n");
    return -1;
  }

  /* initialize network queue */
  if (network_thread_init(ctx) != 0) {
    fprintf(stderr, "initializing rx thread failed\n");
    return -1;
  }

  /* Initialize polled apps and contexts */
  for (i = 0; i < FLEXNIC_PL_APPST_NUM; i++)
  {
    p_app = &ctx->polled_apps[i];
    polled_app_init(p_app, i);
    for (j = 0; j < FLEXNIC_PL_APPST_CTX_NUM; j++)
    {
      p_ctx = &p_app->ctxs[j];
      polled_ctx_init(p_ctx, j);
    }
  }
  ctx->poll_rounds = 0;
  ctx->poll_next_app = 0;
  ctx->act_head = IDXLIST_INVAL;
  ctx->act_tail = IDXLIST_INVAL;

  ctx->evfd = eventfd(0, EFD_NONBLOCK);
  assert(ctx->evfd != -1);
  ctx->ev.epdata.event = EPOLLIN;
  int r = rte_epoll_ctl(RTE_EPOLL_PER_THREAD, EPOLL_CTL_ADD, ctx->evfd, &ctx->ev);
  assert(r == 0);
  fp_state->kctx[ctx->id].evfd = ctx->evfd;

  return 0;
}

static void polled_app_init(struct polled_app *app, uint16_t id)
{
  app->id = id;
  app->next = IDXLIST_INVAL;
  app->prev = IDXLIST_INVAL;
  app->flags = 0;
  app->poll_next_ctx = 0;
  app->act_ctx_head = IDXLIST_INVAL;
  app->act_ctx_tail = IDXLIST_INVAL;
}

static void polled_ctx_init(struct polled_context *ctx, uint32_t id)
{
  ctx->id = id;
  ctx->next = IDXLIST_INVAL;
  ctx->prev = IDXLIST_INVAL;
  ctx->flags = 0;
  ctx->null_rounds = 0;
}

void dataplane_context_destroy(struct dataplane_context *ctx)
{
}

void dataplane_loop(struct dataplane_context *ctx)
{
  struct notify_blockstate nbs;
  uint32_t ts;
  uint64_t cyc, prev_cyc;
  int was_idle = 1;

  notify_canblock_reset(&nbs);
  while (!exited) {
    unsigned n = 0;

    /* count cycles of previous iteration if it was busy */
    prev_cyc = cyc;
    cyc = rte_get_tsc_cycles();
    if (!was_idle)
      ctx->loadmon_cyc_busy += cyc - prev_cyc;

    ts = qman_timestamp(cyc);

    STATS_TS(start);
    n += poll_rx(ctx, ts, cyc);
    STATS_TS(rx);
    tx_flush(ctx);

    n += poll_qman_fwd(ctx, ts);

    STATS_TSADD(ctx, cyc_rx, rx - start);
    n += poll_qman(ctx, ts);
    STATS_TS(qm);
    STATS_TSADD(ctx, cyc_qm, qm - rx);
    n += poll_queues(ctx, ts);
    STATS_TS(qs);
    STATS_TSADD(ctx, cyc_qs, qs - qm);
    n += poll_kernel(ctx, ts);

    /* flush transmit buffer */
    tx_flush(ctx);

    if (ctx->id == 0)
      poll_scale(ctx);

    was_idle = (n == 0);
    if (config.fp_interrupts && notify_canblock(&nbs, !was_idle, cyc)) {
      dataplane_block(ctx, ts);
      notify_canblock_reset(&nbs);
    }
  }
}

static void dataplane_block(struct dataplane_context *ctx, uint32_t ts)
{
  uint32_t max_timeout;
  uint64_t val;
  int ret, i;
  struct rte_epoll_event event[2];

  if (network_rx_interrupt_ctl(&ctx->net, 1) != 0) {
    return;
  }

  max_timeout = qman_next_ts(&ctx->qman, ts);

  ret = rte_epoll_wait(RTE_EPOLL_PER_THREAD, event, 2,
      max_timeout == (uint32_t) -1 ? -1 : max_timeout / 1000);
  if (ret < 0) {
    perror("dataplane_block: rte_epoll_wait failed");
    abort();
  }

  for(i = 0; i < ret; i++) {
    if(event[i].fd == ctx->evfd) {
      ret = read(ctx->evfd, &val, sizeof(uint64_t));
      if ((ret > 0 && ret != sizeof(uint64_t)) ||
          (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
      {
        perror("dataplane_block: read failed");
        abort();
      }
    }
  }
  network_rx_interrupt_ctl(&ctx->net, 0);
}

#ifdef DATAPLANE_STATS
static inline uint64_t read_stat(uint64_t *p)
{
  return __sync_lock_test_and_set(p, 0);
}

void dataplane_dump_stats(void)
{
  struct dataplane_context *ctx;
  unsigned i;

  for (i = 0; i < fp_cores_max; i++) {
    ctx = ctxs[i];
    fprintf(stderr, "dp stats %u: "
        "qm=(%"PRIu64",%"PRIu64",%"PRIu64")  "
        "rx=(%"PRIu64",%"PRIu64",%"PRIu64")  "
        "qs=(%"PRIu64",%"PRIu64",%"PRIu64")  "
        "cyc=(%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64")\n", i,
        read_stat(&ctx->stat_qm_poll), read_stat(&ctx->stat_qm_empty),
        read_stat(&ctx->stat_qm_total),
        read_stat(&ctx->stat_rx_poll), read_stat(&ctx->stat_rx_empty),
        read_stat(&ctx->stat_rx_total),
        read_stat(&ctx->stat_qs_poll), read_stat(&ctx->stat_qs_empty),
        read_stat(&ctx->stat_qs_total),
        read_stat(&ctx->stat_cyc_db), read_stat(&ctx->stat_cyc_qm),
        read_stat(&ctx->stat_cyc_rx), read_stat(&ctx->stat_cyc_qs));
  }
}
#endif

static unsigned poll_rx(struct dataplane_context *ctx, uint32_t ts,
    uint64_t tsc)
{
  int ret;
  unsigned i, n;
  uint8_t freebuf[BATCH_SIZE] = { 0 };
  void *fss[BATCH_SIZE];
  struct tcp_opts tcpopts[BATCH_SIZE];
  struct network_buf_handle *bhs[BATCH_SIZE];

  n = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < n)
    n = TXBUF_SIZE - ctx->tx_num;

  STATS_ADD(ctx, rx_poll, 1);

  /* receive packets */
  ret = network_poll(&ctx->net, n, bhs);
  if (ret <= 0) {
    STATS_ADD(ctx, rx_empty, 1);
    return 0;
  }
  STATS_ADD(ctx, rx_total, n);
  n = ret;

  /* prefetch packet contents (1st cache line) */
  for (i = 0; i < n; i++) {
    rte_prefetch0(network_buf_bufoff(bhs[i]));
  }

  /* look up flow states */
  fast_flows_packet_fss(ctx, bhs, fss, n);

  /* prefetch packet contents (2nd cache line, TS opt overlaps) */
  for (i = 0; i < n; i++) {
    rte_prefetch0(network_buf_bufoff(bhs[i]) + 64);
  }

  /* parse packets */
  fast_flows_packet_parse(ctx, bhs, fss, tcpopts, n);

  for (i = 0; i < n; i++) {
    /* run fast-path for flows with flow state */
    if (fss[i] != NULL) {
      ret = fast_flows_packet(ctx, bhs[i], fss[i], &tcpopts[i], ts);
    } else {
      ret = -1;
    }

    if (ret > 0) {
      freebuf[i] = 1;
    } else if (ret < 0) {
      fast_kernel_packet(ctx, bhs[i]);
    }
  }

  arx_cache_flush(ctx, tsc);

  /* free received buffers */
  for (i = 0; i < n; i++) {
    if (freebuf[i] == 0)
      bufcache_free(ctx, bhs[i]);
  }

  return n;
}

static unsigned poll_queues(struct dataplane_context *ctx, uint32_t ts)
{
  unsigned total;

  if (ctx->poll_rounds % MAX_POLL_ROUNDS == 0)
  {
    total = poll_all_queues(ctx, ts);
  } else 
  {
    total = poll_active_queues(ctx, ts);
  }

  ctx->poll_rounds = ctx->poll_rounds + 1 % MAX_POLL_ROUNDS;
  return total;
}

// TODO: Use a macro to loop over active contexts
/* Polls only the active applications that have not spent more than MAX_NULL_ROUNDS
   without sending data */
static unsigned poll_active_queues(struct dataplane_context *ctx, uint32_t ts)
{
  struct network_buf_handle **handles;
  struct polled_app *act_app;
  struct polled_context *act_ctx;
  void *aqes[BATCH_SIZE];
  unsigned i, total = 0;
  uint32_t cid, aid;
  uint16_t max, j, k = 0, num_bufs = 0;
  int ret;

  STATS_ADD(ctx, qs_poll, 1);

  if (ctx->act_head == IDXLIST_INVAL)
  {
    return 0;
  }

  max = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < max)
    max = TXBUF_SIZE - ctx->tx_num;

  /* allocate buffers contents */
  max = bufcache_prealloc(ctx, max, &handles);

  /* prefetch active contexts */
  aid = ctx->act_head;
  while(aid != IDXLIST_INVAL) 
  {
    act_app = &ctx->polled_apps[aid];
    cid = act_app->act_ctx_head;
    while(cid != IDXLIST_INVAL) 
    {
      act_ctx = &ctx->polled_apps[aid].ctxs[cid];     
      fast_appctx_poll_pf(ctx, cid, aid);
      cid = ctx->polled_apps[aid].ctxs[cid].next;
    }
    aid = ctx->polled_apps[aid].next;
  }

  /* fetch packets from all active contexts */
  aid = ctx->act_head;
  while(aid != IDXLIST_INVAL && k < max) 
  {
    act_app = &ctx->polled_apps[aid];
    cid = act_app->act_ctx_head;
    while(cid != IDXLIST_INVAL && k < max) 
    {
      act_ctx = &act_app->ctxs[cid];
      for (i = 0; i < BATCH_SIZE && k < max; i++) 
      {
        ret = fast_appctx_poll_fetch(ctx, cid, aid, &aqes[k]);
        if (ret == 0)
        {
          k++;
          act_ctx->null_rounds = 0;
        }
        else
        {
          act_ctx->null_rounds += 1;
          if (act_ctx->null_rounds >= MAX_NULL_ROUNDS)
          {
            remove_ctx_from_active(act_app, act_ctx);
          }

          if (act_app->act_ctx_head == IDXLIST_INVAL)
          {
            remove_app_from_active(ctx, act_app);
          }
          break;
        }
        total++;
      }
      cid = act_app->next;
    }
    aid = ctx->polled_apps[aid].next;
  }

  for (j = 0; j < k; j++) 
  {
    ret = fast_appctx_poll_bump(ctx, aqes[j], handles[num_bufs], ts);
    if (ret == 0)
      num_bufs++;
  }

  /* apply buffer reservations */
  bufcache_alloc(ctx, num_bufs);

  /* prove receive queue on all active contexts */
  aid = ctx->act_head;
  while(aid != IDXLIST_INVAL) 
  {
    act_app = &ctx->polled_apps[aid];
    cid = act_app->act_ctx_head;    
    while(cid != IDXLIST_INVAL) 
    {
      act_ctx = &act_app->ctxs[cid];
      fast_actx_rxq_probe(ctx, cid, aid);
      cid = act_ctx->next;
    }
    aid = act_app->next;
  }

  STATS_ADD(ctx, qs_total, total);
  if (total == 0)
    STATS_ADD(ctx, qs_empty, total);

  return total;
}

/* Polls the queues for every applicaiton and context */
static unsigned poll_all_queues(struct dataplane_context *ctx, uint32_t ts)
{
  struct network_buf_handle **handles;
  void *aqes[BATCH_SIZE];
  unsigned n, i, total = 0;
  uint16_t max, k = 0, num_bufs = 0, j;
  uint32_t aid, next_app, next_ctx;
  struct polled_app *app;
  struct polled_context *p_ctx;
  int ret;

  STATS_ADD(ctx, qs_poll, 1);

  max = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < max)
    max = TXBUF_SIZE - ctx->tx_num;

  /* allocate buffers contents */
  max = bufcache_prealloc(ctx, max, &handles);

  for  (aid = 0; aid < FLEXNIC_PL_APPST_NUM; aid++)
  {
    for (n = 0; n < FLEXNIC_PL_APPCTX_NUM; n++) 
    {
      next_app = (ctx->poll_next_app + aid) % FLEXNIC_PL_APPST_NUM;
      next_ctx = (ctx->polled_apps[next_app].poll_next_ctx + n) % FLEXNIC_PL_APPST_CTX_NUM;
      fast_appctx_poll_pf(ctx, next_ctx, next_app);
    }
  }

  /* poll each app in round-robin fashion */
  for (aid = 0; aid < FLEXNIC_PL_APPST_NUM && k < max; aid++)
  {
    for (n = 0; n < FLEXNIC_PL_APPCTX_NUM && k < max; n++) 
    {
      next_app = ctx->poll_next_app;
      app = &ctx->polled_apps[next_app];
      for (i = 0; i < BATCH_SIZE && k < max; i++) 
      {
        next_ctx = ctx->polled_apps[next_app].poll_next_ctx;
        p_ctx = &app->ctxs[next_ctx];
        ret = fast_appctx_poll_fetch(ctx, next_ctx, next_app, &aqes[k]);
        if (ret == 0) 
        {
          p_ctx->null_rounds = 0;
          /* Add app to active list if it is not already in list */
          if ((ctx->polled_apps[next_app].flags & FLAG_ACTIVE) == 0)
          {
            enqueue_app_to_active(ctx, next_app);
          }

          /* Add ctx to active list if it is not already in list */
          if ((ctx->polled_apps[next_app].ctxs[next_ctx].flags & FLAG_ACTIVE) == 0)
          {
            enqueue_ctx_to_active(app, next_ctx);
          }

          k++;
        } else
        {
          p_ctx->null_rounds += 1;
          break;
        }

        total++;
      }

      ctx->polled_apps[next_app].poll_next_ctx = (next_ctx + 1) % FLEXNIC_PL_APPCTX_NUM;
    }
    ctx->poll_next_app = (next_app + 1) % FLEXNIC_PL_APPST_NUM;
  }

  for (j = 0; j < k; j++) 
  {
    ret = fast_appctx_poll_bump(ctx, aqes[j], handles[num_bufs], ts);
    if (ret == 0)
      num_bufs++;
  }

  /* apply buffer reservations */
  bufcache_alloc(ctx, num_bufs);

  for (aid = 0; aid < FLEXNIC_PL_APPST_NUM; aid++)
  {
    for (n = 0; n < FLEXNIC_PL_APPCTX_NUM; n++) 
    { 
      fast_actx_rxq_probe(ctx, n, aid);
    }
  }

  STATS_ADD(ctx, qs_total, total);
  if (total == 0)
    STATS_ADD(ctx, qs_empty, total);

  return total;
}

static void enqueue_ctx_to_active(struct polled_app *act_app, uint32_t cid) 
{
  struct polled_context *new_act;
  uint32_t head, tail;
  
  new_act = &act_app->ctxs[cid];
  head = act_app->act_ctx_head;
  tail = act_app->act_ctx_tail;

  if (head == IDXLIST_INVAL)
  {
    act_app->act_ctx_head = cid; 
  } else {
    act_app->ctxs[act_app->act_ctx_tail].next = cid;
    new_act->prev = tail;
  }

  act_app->act_ctx_tail = cid;
  new_act->flags |= FLAG_ACTIVE;
}

static void remove_ctx_from_active(struct polled_app *act_app, 
    struct polled_context *act_ctx)
{
  if (act_ctx->next == IDXLIST_INVAL && act_ctx->prev == IDXLIST_INVAL)
  {
    act_app->act_ctx_head = IDXLIST_INVAL;
    act_app->act_ctx_tail = IDXLIST_INVAL;
  } else if (act_ctx->next == IDXLIST_INVAL)
  {
    act_app->act_ctx_tail = act_ctx->prev;    
    act_app->ctxs[act_ctx->prev].next = IDXLIST_INVAL;
  } else if (act_ctx->prev == IDXLIST_INVAL)
  {
    act_app->act_ctx_head = act_ctx->next;
    act_app->ctxs[act_ctx->next].prev = IDXLIST_INVAL;
  } else
  {
    act_app->ctxs[act_ctx->prev].next = act_ctx->next;
    act_app->ctxs[act_ctx->next].prev = act_ctx->prev;
  }

  act_ctx->next = IDXLIST_INVAL;
  act_ctx->prev = IDXLIST_INVAL;
  act_ctx->flags &= ~FLAG_ACTIVE;
  act_ctx->null_rounds = 0;
}

static void enqueue_app_to_active(struct dataplane_context *ctx, uint16_t aid)
{
  struct polled_app *new_act;
  uint32_t head, tail;

  new_act = &ctx->polled_apps[aid];
  head = ctx->act_head;
  tail = ctx->act_tail;
  
  if (head == IDXLIST_INVAL)
  {
    ctx->act_head = new_act->id;
  } else
  {
    ctx->polled_apps[tail].next = aid;
    new_act->prev = tail;
  }
  
  ctx->act_tail = new_act->id;
  new_act->flags |= FLAG_ACTIVE; 
}

static void remove_app_from_active(struct dataplane_context *ctx, 
    struct polled_app *act_app)
{
  if (act_app->next == IDXLIST_INVAL && act_app->prev == IDXLIST_INVAL)
  {
    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
  }
  else if (act_app->next == IDXLIST_INVAL)
  {
    ctx->act_tail = act_app->prev;
    ctx->polled_apps[act_app->prev].next = IDXLIST_INVAL; 
  }
  else if (act_app->prev == IDXLIST_INVAL)
  {
    ctx->act_head = act_app->next;
    ctx->polled_apps[act_app->next].prev = IDXLIST_INVAL;
  }
  else 
  {
    ctx->polled_apps[act_app->prev].next = act_app->next;
    ctx->polled_apps[act_app->next].prev = act_app->prev;
  }

  act_app->next = IDXLIST_INVAL;
  act_app->prev = IDXLIST_INVAL;
  act_app->flags &= ~FLAG_ACTIVE;
  act_app->act_ctx_head = IDXLIST_INVAL;
  act_app->act_ctx_tail = IDXLIST_INVAL;
}

static unsigned poll_kernel(struct dataplane_context *ctx, uint32_t ts)
{
  struct network_buf_handle **handles;
  unsigned total = 0;
  uint16_t max, k = 0;
  int ret;

  max = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < max)
    max = TXBUF_SIZE - ctx->tx_num;

  max = (max > 8 ? 8 : max);
  /* allocate buffers contents */
  max = bufcache_prealloc(ctx, max, &handles);

  for (k = 0; k < max;) {
    ret = fast_kernel_poll(ctx, handles[k], ts);
 
    if (ret == 0)
      k++;
    else if (ret < 0)
      break;

    total++;
  }

  /* apply buffer reservations */
  bufcache_alloc(ctx, k);

  return total;
}

static unsigned poll_qman(struct dataplane_context *ctx, uint32_t ts)
{
  unsigned q_ids[BATCH_SIZE];
  unsigned aq_ids[BATCH_SIZE];
  uint16_t q_bytes[BATCH_SIZE];
  struct network_buf_handle **handles;
  uint16_t off = 0, max;
  int ret, i, use;

  max = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < max)
    max = TXBUF_SIZE - ctx->tx_num;

  STATS_ADD(ctx, qm_poll, 1);

  /* allocate buffers contents */
  max = bufcache_prealloc(ctx, max, &handles);

  /* poll queue manager */
  ret = qman_poll(&ctx->qman, max, aq_ids, q_ids, q_bytes);
  if (ret <= 0) {
    STATS_ADD(ctx, qm_empty, 1);
    return 0;
  }

  STATS_ADD(ctx, qm_total, ret);

  for (i = 0; i < ret; i++) {
    rte_prefetch0(handles[i]);
  }

  for (i = 0; i < ret; i++) {
    rte_prefetch0((uint8_t *) handles[i] + 64);
  }

  /* prefetch packet contents */
  for (i = 0; i < ret; i++) {
    rte_prefetch0(network_buf_buf(handles[i]));
  }

  fast_flows_qman_pf(ctx, q_ids, ret);

  fast_flows_qman_pfbufs(ctx, q_ids, ret);

  for (i = 0; i < ret; i++) {
    use = fast_flows_qman(ctx, aq_ids[i], q_ids[i], handles[off], ts);

    if (use == 0)
     off++;
  }

  /* apply buffer reservations */
  bufcache_alloc(ctx, off);

  return ret;
}

static unsigned poll_qman_fwd(struct dataplane_context *ctx, uint32_t ts)
{
  void *flow_states[4 * BATCH_SIZE];
  int ret, i;

  /* poll queue manager forwarding ring */
  ret = rte_ring_dequeue_burst(ctx->qman_fwd_ring, flow_states, 4 * BATCH_SIZE, NULL);
  for (i = 0; i < ret; i++) {
    fast_flows_qman_fwd(ctx, flow_states[i]);
  }

  return ret;
}

static inline uint8_t bufcache_prealloc(struct dataplane_context *ctx, uint16_t num,
    struct network_buf_handle ***handles)
{
  uint16_t grow, res, head, g, i;
  struct network_buf_handle *nbh;
  /* try refilling buffer cache */
  if (ctx->bufcache_num < num) {
    grow = BUFCACHE_SIZE - ctx->bufcache_num;
    head = (ctx->bufcache_head + ctx->bufcache_num) & (BUFCACHE_SIZE - 1);
    
    if (head + grow <= BUFCACHE_SIZE) {
      res = network_buf_alloc(&ctx->net, grow, ctx->bufcache_handles + head);
    } else {
      g = BUFCACHE_SIZE - head;
      res = network_buf_alloc(&ctx->net, g, ctx->bufcache_handles + head);
      
      if (res == g) {
        res += network_buf_alloc(&ctx->net, grow - g, ctx->bufcache_handles);
      }
    }

    for (i = 0; i < res; i++) {
      g = (head + i) & (BUFCACHE_SIZE - 1);
      nbh = ctx->bufcache_handles[g];
      ctx->bufcache_handles[g] = (struct network_buf_handle *)
        ((uintptr_t) nbh);
    }

    ctx->bufcache_num += res;
  }
  num = MIN(num, (ctx->bufcache_head + ctx->bufcache_num <= BUFCACHE_SIZE ?
        ctx->bufcache_num : BUFCACHE_SIZE - ctx->bufcache_head));


  *handles = ctx->bufcache_handles + ctx->bufcache_head;

  return num;
}

static inline void bufcache_alloc(struct dataplane_context *ctx, uint16_t num)
{
  assert(num <= ctx->bufcache_num);

  ctx->bufcache_head = (ctx->bufcache_head + num) & (BUFCACHE_SIZE - 1);
  ctx->bufcache_num -= num;
}

static inline void bufcache_free(struct dataplane_context *ctx,
    struct network_buf_handle *handle)
{
  uint32_t head, num;

  num = ctx->bufcache_num;
  if (num < BUFCACHE_SIZE) {
    /* free to cache */
    head = (ctx->bufcache_head + num) & (BUFCACHE_SIZE - 1);
    ctx->bufcache_handles[head] = handle;
    ctx->bufcache_num = num + 1;
    network_buf_reset(handle);
  } else {
    /* free to network buffer manager */
    network_free(1, &handle);
  }
}

static inline void tx_flush(struct dataplane_context *ctx)
{
  int ret;
  unsigned i;

  if (ctx->tx_num == 0) {
    return;
  }

  /* try to send out packets */
  ret = network_send(&ctx->net, ctx->tx_num, ctx->tx_handles);

  if (ret == ctx->tx_num) {
    /* everything sent */
    ctx->tx_num = 0;
  } else if (ret > 0) {
    /* move unsent packets to front */
    for (i = ret; i < ctx->tx_num; i++) {
      ctx->tx_handles[i - ret] = ctx->tx_handles[i];
    }
    ctx->tx_num -= ret;
  }
}

static void poll_scale(struct dataplane_context *ctx)
{
  unsigned st = fp_scale_to;

  if (st == 0)
    return;

  fprintf(stderr, "Scaling fast path from %u to %u\n", fp_cores_cur, st);
  if (st < fp_cores_cur) {
    if (network_scale_down(fp_cores_cur, st) != 0) {
      fprintf(stderr, "network_scale_down failed\n");
      abort();
    }
  } else if (st > fp_cores_cur) {
    if (network_scale_up(fp_cores_cur, st) != 0) {
      fprintf(stderr, "network_scale_up failed\n");
      abort();
    }
  } else {
    fprintf(stderr, "poll_scale: warning core number didn't change\n");
  }

  fp_cores_cur = st;
  fp_scale_to = 0;
}

static void arx_cache_flush(struct dataplane_context *ctx, uint64_t tsc)
{
  uint16_t i, aid;
  struct flextcp_pl_appctx *actx;
  struct flextcp_pl_arx *parx[BATCH_SIZE];

  for (i = 0; i < ctx->arx_num; i++) {
    aid = ctx->arx_ctx_appid[i];
    actx = &fp_state->appctx[ctx->id][aid][ctx->arx_ctx[i]];
    if (fast_actx_rxq_alloc(ctx, actx, &parx[i]) != 0) {
      /* TODO: how do we handle this? */
      fprintf(stderr, "arx_cache_flush: no space in app rx queue\n");
      abort();
    }
  }

  for (i = 0; i < ctx->arx_num; i++) {
    rte_prefetch0(parx[i]);
  }

  for (i = 0; i < ctx->arx_num; i++) {
    *parx[i] = ctx->arx_cache[i];
  }

  for (i = 0; i < ctx->arx_num; i++) {
    aid = ctx->arx_ctx_appid[i];
    actx = &fp_state->appctx[ctx->id][aid][ctx->arx_ctx[i]];
    notify_appctx(actx, tsc);
  }

  ctx->arx_num = 0;
}
