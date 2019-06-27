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


static unsigned poll_rx(struct dataplane_context *ctx, uint32_t ts) __attribute__((noinline));
static unsigned poll_queues(struct dataplane_context *ctx, uint32_t ts)  __attribute__((noinline));
static unsigned poll_kernel(struct dataplane_context *ctx, uint32_t ts) __attribute__((noinline));
static unsigned poll_qman(struct dataplane_context *ctx, uint32_t ts) __attribute__((noinline));
static unsigned poll_qman_fwd(struct dataplane_context *ctx, uint32_t ts) __attribute__((noinline));
static void poll_scale(struct dataplane_context *ctx);

static inline uint8_t bufcache_prealloc(struct dataplane_context *ctx, uint16_t num,
    struct network_buf_handle ***handles);
static inline void bufcache_alloc(struct dataplane_context *ctx, uint16_t num);
static inline void bufcache_free(struct dataplane_context *ctx,
    struct network_buf_handle *handle);

static inline void tx_flush(struct dataplane_context *ctx);
static inline void tx_send(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, uint16_t off, uint16_t len);

static void arx_cache_flush(struct dataplane_context *ctx, uint32_t ts) __attribute__((noinline));

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
  char name[32];

  /* initialize forwarding queue */
  sprintf(name, "qman_fwd_ring_%u", ctx->id);
  if ((ctx->qman_fwd_ring = rte_ring_create(name, 32 * 1024, rte_socket_id(),
          RING_F_SC_DEQ)) == NULL)
  {
    fprintf(stderr, "initializing rte_ring_create");
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

  ctx->poll_next_ctx = ctx->id;

  ctx->evfd = eventfd(0, 0);
  assert(ctx->evfd != -1);
  ctx->ev.epdata.event = EPOLLIN;
  int r = rte_epoll_ctl(RTE_EPOLL_PER_THREAD, EPOLL_CTL_ADD, ctx->evfd, &ctx->ev);
  assert(r == 0);
  fp_state->kctx[ctx->id].evfd = ctx->evfd;

  return 0;
}

void dataplane_context_destroy(struct dataplane_context *ctx)
{
}

void dataplane_loop(struct dataplane_context *ctx)
{
  uint32_t ts, startwait = 0;
  uint64_t cyc, prev_cyc;
  int was_idle = 1;

  while (!exited) {
    unsigned n = 0;

    /* count cycles of previous iteration if it was busy */
    prev_cyc = cyc;
    cyc = rte_get_tsc_cycles();
    if (!was_idle)
      ctx->loadmon_cyc_busy += cyc - prev_cyc;


    ts = qman_timestamp(cyc);

    STATS_TS(start);
    n += poll_rx(ctx, ts);
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

    if(UNLIKELY(n == 0)) {
      was_idle = 1;

      if(startwait == 0) {
	startwait = ts;
      } else if (config.fp_interrupts && ts - startwait >= POLL_CYCLE) {
	// Idle -- wait for interrupt or data from apps/kernel
	int r = network_rx_interrupt_ctl(&ctx->net, 1);

	// Only if device running
	if(r == 0) {
	  uint32_t timeout_us = qman_next_ts(&ctx->qman, ts);
	  /* fprintf(stderr, "[%u] fastemu idle - timeout %d ms\n", ctx->core, */
	  /* 	  timeout_us == (uint32_t)-1 ? -1 : timeout_us / 1000); */
	  struct rte_epoll_event event[2];
	  int n = rte_epoll_wait(RTE_EPOLL_PER_THREAD, event, 2,
				 timeout_us == (uint32_t)-1 ? -1 : timeout_us / 1000);
	  assert(n != -1);
	  /* fprintf(stderr, "[%u] fastemu busy - %u events\n", ctx->core, n); */
	  for(int i = 0; i < n; i++) {
	    if(event[i].fd == ctx->evfd) {
	      /* fprintf(stderr, "[%u] fastemu - woken up by event FD = %d\n", */
	      /* 	      ctx->core, event[i].fd); */
	      uint64_t val;
	      int r = read(ctx->evfd, &val, sizeof(uint64_t));
	      assert(r == sizeof(uint64_t));
	    /* } else { */
	    /*   fprintf(stderr, "[%u] fastemu - woken up by RX interrupt FD = %d\n", */
	    /* 	      ctx->core, event[i].fd); */
	    }
	  }

          /*fprintf(stderr, "dataplane_loop: woke up %u n=%u fd=%d evfd=%d\n", ctx->id, n, event[0].fd, ctx->evfd);*/
	   network_rx_interrupt_ctl(&ctx->net, 0);
	}
      startwait = 0;
      }
    } else {
      was_idle = 0;
      startwait = 0;
    }
  }
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

static unsigned poll_rx(struct dataplane_context *ctx, uint32_t ts)
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

  arx_cache_flush(ctx, ts);

  /* free received buffers */
  for (i = 0; i < n; i++) {
    if (freebuf[i] == 0)
      bufcache_free(ctx, bhs[i]);
  }

  return n;
}

static unsigned poll_queues(struct dataplane_context *ctx, uint32_t ts)
{
  struct network_buf_handle **handles;
  void *aqes[BATCH_SIZE];
  unsigned n, i, total = 0;
  uint16_t max, k = 0, num_bufs = 0, j;
  int ret;

  STATS_ADD(ctx, qs_poll, 1);

  max = BATCH_SIZE;
  if (TXBUF_SIZE - ctx->tx_num < max)
    max = TXBUF_SIZE - ctx->tx_num;

  /* allocate buffers contents */
  max = bufcache_prealloc(ctx, max, &handles);

  for (n = 0; n < FLEXNIC_PL_APPCTX_NUM; n++) {
    fast_appctx_poll_pf(ctx, (ctx->poll_next_ctx + n) % FLEXNIC_PL_APPCTX_NUM);
  }

  for (n = 0; n < FLEXNIC_PL_APPCTX_NUM && k < max; n++) {
    for (i = 0; i < BATCH_SIZE && k < max; i++) {
      ret = fast_appctx_poll_fetch(ctx, ctx->poll_next_ctx, &aqes[k]);
      if (ret == 0)
        k++;
      else
        break;

      total++;
    }

    ctx->poll_next_ctx = (ctx->poll_next_ctx + 1) %
      FLEXNIC_PL_APPCTX_NUM;
  }

  for (j = 0; j < k; j++) {
    ret = fast_appctx_poll_bump(ctx, aqes[j], handles[num_bufs], ts);
    if (ret == 0)
      num_bufs++;
  }

  /* apply buffer reservations */
  bufcache_alloc(ctx, num_bufs);

  for (n = 0; n < FLEXNIC_PL_APPCTX_NUM; n++)
    fast_actx_rxq_probe(ctx, n);

  STATS_ADD(ctx, qs_total, total);
  if (total == 0)
    STATS_ADD(ctx, qs_empty, total);

  return total;
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
  ret = qman_poll(&ctx->qman, max, q_ids, q_bytes);
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
    use = fast_flows_qman(ctx, q_ids[i], handles[off], ts);

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

static void arx_cache_flush(struct dataplane_context *ctx, uint32_t ts)
{
  uint16_t i;
  struct flextcp_pl_appctx *actx;
  struct flextcp_pl_arx *parx[BATCH_SIZE];

  for (i = 0; i < ctx->arx_num; i++) {
    actx = &fp_state->appctx[ctx->id][ctx->arx_ctx[i]];
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
    actx = &fp_state->appctx[ctx->id][ctx->arx_ctx[i]];
    actx_kick(actx, ts);
  }

  ctx->arx_num = 0;
}
