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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <utils.h>

#include <tas.h>
#include "internal.h"

#define CONF_MSS 1400

int cc_init(void)
{
  return 0;
}

static inline void issue_retransmits(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t cur_ts);

static inline void dctcp_win_init(struct connection *c);
static inline void dctcp_win_update(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t diff_ts, uint32_t cur_ts);

static inline void dctcp_rate_init(struct connection *c);
static inline void dctcp_rate_update(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t diff_ts, uint32_t cur_ts);

static inline void timely_init(struct connection *c);
static inline void timely_update(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t diff_ts, uint32_t cur_ts);

static inline void const_rate_init(struct connection *c);
static inline void const_rate_update(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t diff_ts, uint32_t cur_ts);

static inline uint32_t window_to_rate(uint32_t window, uint32_t rtt);

static uint32_t last_ts = 0;
static struct connection *cc_conns = NULL;
static struct connection *next_conn = NULL;

uint32_t cc_next_ts(uint32_t cur_ts)
{
  struct connection *c;
  assert(cur_ts >= last_ts);
  uint32_t ts = -1U;

  for (c = cc_conns; c != NULL; c = c->cc_next) {
    if (c->status != CONN_OPEN)
      continue;

    int32_t next_ts = (c->cc_rtt * config.cc_control_interval) - (cur_ts - c->cc_last_ts);
    if(next_ts >= 0) {
      ts = MIN(ts, next_ts);
    } else {
      ts = 0;
    }
  }

  return (ts == -1U ? -1U : MAX(ts, config.cc_control_granularity - (cur_ts - last_ts)));
}

unsigned cc_poll(uint32_t cur_ts)
{
  struct connection *c, *c_first;
  struct nicif_connection_stats stats;
  uint32_t diff_ts;
  uint32_t last;
  unsigned n = 0;

  diff_ts = cur_ts - last_ts;
  if (0 && diff_ts < config.cc_control_granularity)
    return 0;

  c = c_first = (next_conn != NULL ? next_conn : cc_conns);
  if (c == NULL) {
    last_ts = cur_ts;
    return 0;
  }

  for (; n < 128 && (n == 0 || c != c_first);
      c = (c->cc_next != NULL ? c->cc_next : cc_conns), n++)
  {
    if (c->status != CONN_OPEN)
      continue;

    if (cur_ts - c->cc_last_ts < c->cc_rtt * config.cc_control_interval)
      continue;

    if (nicif_connection_stats(c->flow_id, &stats)) {
      fprintf(stderr, "cc_poll: nicif_connection_stats failed unexpectedly\n");
      abort();
    }

    /* calculate difference to last time */
    last = c->cc_last_drops;
    c->cc_last_drops = stats.c_drops;
    stats.c_drops -= last;

    last = c->cc_last_acks;
    c->cc_last_acks = stats.c_acks;
    stats.c_acks -= last;

    last = c->cc_last_ackb;
    c->cc_last_ackb = stats.c_ackb;
    stats.c_ackb -= last;

    last = c->cc_last_ecnb;
    c->cc_last_ecnb = stats.c_ecnb;
    stats.c_ecnb -= last;

    kstats.drops += stats.c_drops;
    kstats.ecn_marked += stats.c_ecnb;
    kstats.acks += stats.c_ackb;

    switch (config.cc_algorithm) {
      case CONFIG_CC_DCTCP_WIN:
        dctcp_win_update(c, &stats, diff_ts, cur_ts);
        break;

      case CONFIG_CC_DCTCP_RATE:
        dctcp_rate_update(c, &stats, diff_ts, cur_ts);
        break;

      case CONFIG_CC_TIMELY:
        timely_update(c, &stats, diff_ts, cur_ts);
        break;

      case CONFIG_CC_CONST_RATE:
        const_rate_update(c, &stats, diff_ts, cur_ts);
        break;

      default:
        fprintf(stderr, "cc_poll: unknown CC algorithm (%u)\n",
            config.cc_algorithm);
        abort();
        break;
    }

    issue_retransmits(c, &stats, cur_ts);
    nicif_connection_setrate(c->flow_id, c->cc_rate);

    c->cc_last_ts = cur_ts;

  }

  next_conn = c;
  last_ts = cur_ts;
  return n;
}

void cc_conn_init(struct connection *conn)
{
  conn->cc_next = cc_conns;
  cc_conns = conn;

  conn->cc_last_ts = cur_ts;
  conn->cc_rtt = config.tcp_rtt_init;
  conn->cc_rexmits = 0;

  switch (config.cc_algorithm) {
    case CONFIG_CC_DCTCP_WIN:
      dctcp_win_init(conn);
      break;

    case CONFIG_CC_DCTCP_RATE:
      dctcp_rate_init(conn);
      break;

    case CONFIG_CC_TIMELY:
      timely_init(conn);
      break;

    case CONFIG_CC_CONST_RATE:
      const_rate_init(conn);
      break;

    default:
      fprintf(stderr, "cc_conn_init: unknown CC algorithm (%u)\n",
          config.cc_algorithm);
      abort();
      break;
  }
}

void cc_conn_remove(struct connection *conn)
{
  struct connection *cp = NULL;

  if (next_conn == conn) {
    next_conn = conn->cc_next;
  }

  if (cc_conns == conn) {
    cc_conns = conn->cc_next;
  } else {
    for (cp = cc_conns; cp != NULL && cp->cc_next != conn;
        cp = cp->cc_next);
    if (cp == NULL) {
      fprintf(stderr, "conn_unregister: connection not found\n");
      abort();
    }

    cp->cc_next = conn->cc_next;
  }
}

static inline void issue_retransmits(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t cur_ts)
{
  uint32_t rtt = (stats->rtt != 0 ? stats->rtt : config.tcp_rtt_init);

  /* check for re-transmits */
  if (stats->txp && stats->c_ackb == 0) {
    if (c->cnt_tx_pending++ == 0) {
      c->ts_tx_pending = cur_ts;
    } else if (c->cnt_tx_pending >= config.cc_rexmit_ints &&
        (cur_ts - c->ts_tx_pending) >= 2 * rtt)
    {
      if (nicif_connection_retransmit(c->flow_id, c->flow_group) == 0) {
        c->cnt_tx_pending = 0;
        kstats.kernel_rexmit++;
        c->cc_rexmits++;
      }
    }
  } else {
    c->cnt_tx_pending = 0;
  }
}

/******************************************************************************/
/* Window-based DCTCP */

static inline void dctcp_win_init(struct connection *c)
{
  struct connection_cc_dctcp_win *cc = &c->cc.dctcp_win;

  cc->window = 2 * CONF_MSS;
  c->cc_rate = window_to_rate(cc->window, config.tcp_rtt_init);
  cc->ecn_rate = 0;
  cc->slowstart = 1;
}

static inline void dctcp_win_update(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t diff_ts, uint32_t cur_ts)
{
  struct connection_cc_dctcp_win *cc = &c->cc.dctcp_win;
  uint64_t ecn_rate, incr;
  uint32_t rtt = stats->rtt, win = cc->window;

  assert(win >= CONF_MSS);

  /* If RTT is zero, use estimate */
  if (rtt == 0) {
    rtt = config.tcp_rtt_init;
  }

  /* Slow start */
  if (cc->slowstart) {
    if (stats->c_drops == 0 && stats->c_ecnb == 0 && c->cc_rexmits == 0) {
      /* double window, but ensure we don't overflow it */
      if (win + stats->c_ackb > win)
        win += stats->c_ackb;
    } else {
      /* if we see any indication of congestion go into congestion avoidance */
      cc->slowstart = 0;
    }
  }

  /* Congestion avoidance */
  if (!cc->slowstart) {
    /* if we have drops, cut by half */
    if (stats->c_drops > 0 || c->cc_rexmits > 0) {
      win /= 2;
    } else {
      /* update ECN rate */
      if (stats->c_ackb > 0) {
        stats->c_ecnb = (stats->c_ecnb <= stats->c_ackb ? stats->c_ecnb :
            stats->c_ackb);
        ecn_rate = (((uint64_t) stats->c_ecnb) * UINT32_MAX) / stats->c_ackb;

        /* EWMA */
        ecn_rate = ((ecn_rate * config.cc_dctcp_weight) +
            ((uint64_t) cc->ecn_rate *
             (UINT32_MAX - config.cc_dctcp_weight)));
        ecn_rate /= UINT32_MAX;
        cc->ecn_rate = ecn_rate;
      }

      /* if ecn marks: reduce window */
      if (stats->c_ecnb > 0) {
        win = (((uint64_t) win) * (UINT32_MAX - cc->ecn_rate / 2)) / UINT32_MAX;
      } else {
        /* additive increase */
        assert(win != 0);
        incr = ((uint64_t) stats->c_ackb * CONF_MSS) / win;
        if ((uint32_t) (win + incr) > win)
          win += incr;
      }
    }
  }

  /* Ensure window is at least 1 mss */
  if (win < CONF_MSS)
    win = CONF_MSS;

  /* A window larger than the send buffer also does not make much sense */
  if (win > c->tx_len)
    win = c->tx_len;

  c->cc_rtt = rtt;
  c->cc_rate = window_to_rate(win, rtt);
  assert(win >= CONF_MSS);
  cc->window = win;
  c->cc_rexmits = 0;
}

/** Convert window in bytes to kbps */
static inline uint32_t window_to_rate(uint32_t window, uint32_t rtt)
{
  uint64_t time, rate;

  /* calculate how long [ns] it will take to send a window size's worth */
  time = (((uint64_t) window * 8 * 1000) / config.tcp_link_bw) / 1000;

  /* we won't be able to send more than a window per rtt */
  if (time < rtt * 1000)
    time = rtt * 1000;

  /* convert time to rate */
  assert(time != 0);
  rate = ((uint64_t) window * 8 * 1000000) / time;
  if (rate > UINT32_MAX)
    rate = UINT32_MAX;

  return rate;
}

/******************************************************************************/
/* Rate-based DCTCP */

static inline void dctcp_rate_init(struct connection *c)
{
  struct connection_cc_dctcp_rate *cc = &c->cc.dctcp_rate;

  c->cc_rate = config.cc_dctcp_init;
  cc->ecn_rate = 0;
  cc->slowstart = 1;

  cc->unproc_ecnb = 0;
  cc->unproc_acks = 0;
  cc->unproc_ackb = 0;
  cc->unproc_drops = 0;

}

static inline void dctcp_rate_update(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t diff_ts, uint32_t cur_ts)
{
  struct connection_cc_dctcp_rate *cc = &c->cc.dctcp_rate;
  uint64_t ecn_rate;
  uint32_t act_rate, rate = c->cc_rate, rtt = stats->rtt, c_ecnb, c_acks,
           c_ackb, c_drops;

  /* If RTT is zero, use estimate */
  if (rtt == 0) {
    rtt = config.tcp_rtt_init;
  }
  c->cc_rtt = rtt;

  c_ecnb = cc->unproc_ecnb + stats->c_ecnb;
  c_acks = cc->unproc_acks + stats->c_acks;
  c_ackb = cc->unproc_ackb + stats->c_ackb;
  c_drops = cc->unproc_drops + stats->c_drops;

  if (c_acks < config.cc_dctcp_minpkts) {
    cc->unproc_ecnb = c_ecnb;
    cc->unproc_acks = c_acks;
    cc->unproc_ackb = c_ackb;
    cc->unproc_drops = c_drops;
    return;
  } else {
    cc->unproc_ecnb = 0;
    cc->unproc_acks = 0;
    cc->unproc_ackb = 0;
    cc->unproc_drops = 0;
  }

  /* calculate actual rate */
  if (c->cc_last_ts != 0) {
    act_rate = c_ackb * 8 * 1000 / (cur_ts - c->cc_last_ts);
  } else {
    act_rate = 0;
  }
  cc->act_rate = (7 * cc->act_rate + act_rate) / 8;
  act_rate = (act_rate >= cc->act_rate ? act_rate : cc->act_rate);

  /* clamp rate to actually used rate * 1.2 */
  if (rate > (uint64_t) act_rate * 12 / 10) {
    rate = (uint64_t) act_rate * 12 / 10;
  }

  /* Slow start */
  if (cc->slowstart) {
    if (c_drops == 0 && c_ecnb == 0 && c->cc_rexmits == 0) {
      /* double rate*/
      if (rate * 2 >= rate)
        rate *= 2;
      else
        rate = UINT32_MAX;
    } else {
      /* if we see any indication of congestion go into congestion avoidance */
      cc->slowstart = 0;
    }
  }

  /* Congestion avoidance */
  if (!cc->slowstart) {
    /* if we have drops, cut by half */
    if (c_drops > 0 || c->cc_rexmits > 0) {
      rate /= 2;
    } else {
      /* update ECN rate */
      if (c_ackb > 0) {
        c_ecnb = (c_ecnb <= c_ackb ? c_ecnb : c_ackb);
        ecn_rate = (((uint64_t) c_ecnb) * UINT32_MAX) / c_ackb;

        /* EWMA */
        ecn_rate = ((ecn_rate * config.cc_dctcp_weight) +
            ((uint64_t) cc->ecn_rate *
             (UINT32_MAX - config.cc_dctcp_weight)));
        ecn_rate /= UINT32_MAX;
        cc->ecn_rate = ecn_rate;
      }

      /* if ecn marks: reduce window */
      if (c_ecnb > 0) {
        rate = (((uint64_t) rate) * (UINT32_MAX - cc->ecn_rate / 2)) /
            UINT32_MAX;
      } else if (config.cc_dctcp_mimd == 0) {
        /* additive increase */
        rate += config.cc_dctcp_step;
      } else {
        /* multiplicative increase */
        rate += (((uint64_t) rate) * config.cc_dctcp_mimd) / UINT32_MAX;
      }
    }
  }

  /* ensure we're at least at the minimum rate */
  if (rate < config.cc_dctcp_min)
    rate = config.cc_dctcp_min;

  c->cc_rate = rate;
  c->cc_rexmits = 0;
}

/******************************************************************************/
/* TIMELY */

static inline void timely_init(struct connection *c)
{
  struct connection_cc_timely *cc = &c->cc.timely;
  c->cc_rate = config.cc_timely_init;
  cc->rtt_prev = cc->rtt_diff = cc->hai_cnt = 0;
  cc->last_ts = 0;
  cc->slowstart = 1;
}

static inline void timely_update(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t diff_ts, uint32_t cur_ts)
{
  struct connection_cc_timely *cc = &c->cc.timely;
  int32_t new_rtt_diff = 0;
  uint32_t new_rtt, new_rate, act_rate;
  uint64_t factor;
  int64_t x, normalized_gradient = 0;

  new_rtt = stats->rtt;

  /* calculate actual rate */
  if (cc->last_ts != 0) {
    act_rate = stats->c_ackb * 8 * 1000 / (cur_ts - cc->last_ts);
  } else {
    act_rate = 0;
  }
  cc->act_rate = (7 * cc->act_rate + act_rate) / 8;
  act_rate = (act_rate >= cc->act_rate ? act_rate : cc->act_rate);

  /* no rtt estimate yet, a bit weird */
  if (new_rtt == 0)
    return;

  /* if in slow-start and rtt is above Thigh, exit slow-start */
  if (cc->slowstart && new_rtt > (config.cc_timely_tlow + config.cc_timely_thigh) / 2) {
    cc->slowstart = 0;
  }

  /* clamp rate to actually used rate * 1.2 */
  if (!cc->slowstart && c->cc_rate > (uint64_t) act_rate * 12 / 10) {
    c->cc_rate = (uint64_t) act_rate * 12 / 10;
  }

  /* can only calculate a gradient if we have a previous rtt */
  if (cc->rtt_prev != 0) {
    new_rtt_diff = new_rtt - cc->rtt_prev;

    /* calculate rtt_diff */
    factor = config.cc_timely_alpha / 2;
    x = (INT32_MAX - factor) * cc->rtt_diff + factor * new_rtt_diff;
    cc->rtt_diff = x / INT32_MAX;

    /* calculate normalized_gradient */
    normalized_gradient =
        (int64_t) cc->rtt_diff * INT16_MAX / config.cc_timely_min_rtt;
  }
  cc->rtt_prev = new_rtt;


  uint32_t orig_rate = c->cc_rate;
  if (cc->slowstart) {
    c->cc_rate *= 2;
  } else if (new_rtt < config.cc_timely_tlow) {
    new_rate = c->cc_rate;
    new_rate += config.cc_timely_step;
    c->cc_rate = new_rate;
    cc->hai_cnt = 0;

  } else if (new_rtt > config.cc_timely_thigh) {
    /* rate *= 1 - beta * (1 - Thigh/rtt)
     * = 1 - a, a = beta * b, b = 1 - d, d = Thigh/rtt */

    uint32_t d = ((uint64_t) UINT32_MAX * config.cc_timely_thigh) / new_rtt;
    uint32_t b = UINT32_MAX - d;
    uint32_t a = (((uint64_t) config.cc_timely_beta) * b) / UINT32_MAX;
    c->cc_rate = (((uint64_t) c->cc_rate) * (UINT32_MAX - a)) / UINT32_MAX;
    cc->hai_cnt = 0;
  } else if (normalized_gradient <= 0) {
    if (++cc->hai_cnt >= 5) {
      c->cc_rate += config.cc_timely_step * 5;
      cc->hai_cnt--;
    } else {
      c->cc_rate += config.cc_timely_step;
    }
  } else {
    /* rate *= 1 - beta * (normalized_gradient)
     * = 1 - a, a = beta * normalized_gradient */

    int64_t a = ((int64_t) (config.cc_timely_beta / 2)) * normalized_gradient;
    int64_t b = a / INT16_MAX;
    int64_t d = (b <= INT32_MAX ? INT32_MAX - b : 0);
    int64_t e = ((int64_t) (uint64_t) c->cc_rate) * d;
    int64_t f = e / INT32_MAX;

    c->cc_rate = f;

    cc->hai_cnt = 0;
  }

  if (c->cc_rate < orig_rate / 2) {
    c->cc_rate = orig_rate / 2;
  }

  if (c->cc_rate < config.cc_timely_min_rate)
    c->cc_rate = config.cc_timely_min_rate;

  c->cc_rtt = stats->rtt;
  cc->last_ts = cur_ts;
  c->cc_rexmits = 0;
}

/******************************************************************************/
/* Constant rate */

static inline void const_rate_init(struct connection *c)
{
  c->cc_rate = config.cc_const_rate;
}

static inline void const_rate_update(struct connection *c,
    struct nicif_connection_stats *stats, uint32_t diff_ts, uint32_t cur_ts)
{
  c->cc_rtt = (stats->rtt != 0 ? stats->rtt : config.tcp_rtt_init);
  c->cc_rexmits = 0;
}
