#ifndef TASSTATS_H_
#define TASSTATS_H_

#include <stdint.h>
#include <rte_cycles.h>

#ifdef PROFILING
#define DATAPLANE_STATS 1
#define CONTROLPLANE_STATS 1
#endif

#ifdef DATAPLANE_STATS

#define STATS_TS(n) uint64_t n = rte_get_tsc_cycles()
#define STATS_ATOMIC_ADD(c, f, n) __sync_fetch_and_add(&c->stats.stat_##f, n)
#define STATS_ADD(c, f, n) (c->stats.stat_##f += n)

#define STATS_ATOMIC_FETCHRESET(c, f) (__sync_lock_test_and_set(&c->stats.stat_##f, 0))
#define STATS_ATOMIC_FETCH(c, f) STATS_ATOMIC_ADD(c, f, 0)
#define STATS_FETCH(c, f) (c->stats.stat_##f)

#else

#define STATS_TS(n) do { } while (0)
#define STATS_ATOMIC_ADD(c, f, n) do { } while (0)
#define STATS_ADD(c, f, n) do { } while (0)

#define STATS_ATOMIC_FETCHRESET(c, f) (0)
#define STATS_ATOMIC_FETCH(c, f) (0)
#define STATS_FETCH(c, f) (0)

#endif

struct dataplane_stats
{
#ifdef DATAPLANE_STATS
  /* Fastpath Stats
   * Poll: # of polls
   * Empty: # of empty polls
   * Total: # of events processed
   */

  /* Queue Manager */
  uint64_t stat_qm_poll;
  uint64_t stat_qm_empty;
  uint64_t stat_qm_total;

  /* NIC RX/TX */
  uint64_t stat_rx_poll;
  uint64_t stat_rx_empty;
  uint64_t stat_rx_total;
  uint64_t stat_tx_poll;
  uint64_t stat_tx_empty;
  uint64_t stat_tx_total;

  /* Application Queue */
  uint64_t stat_qs_poll;
  uint64_t stat_qs_empty;
  uint64_t stat_qs_total;

  /* Slow Path */
  uint64_t stat_sp_poll;
  uint64_t stat_sp_empty;
  uint64_t stat_sp_total;

  /* Slow path RX queue */
  uint64_t stat_sprx_cycles;
  uint64_t stat_sprx_count;

  /* Cycles consumed in processing by modules */
  uint64_t stat_cyc_qm;
  uint64_t stat_cyc_rx;
  uint64_t stat_cyc_qs;
  uint64_t stat_cyc_sp;
  uint64_t stat_cyc_tx;

#ifdef QUEUE_STATS
  /* Kernel -> Fastpath queue delay statistics */
  uint64_t stat_kin_cycles;
  uint64_t stat_kin_count;
#endif

#endif
};

struct controlplane_stats
{
#ifdef CONTROLPLANE_STATS
  /* Fastpath Stats
   * Poll: # of polls
   * Empty: # of empty polls
   * Total: # of events processed
   */

  /* NIC RX */
  uint64_t stat_rx_poll;
  uint64_t stat_rx_empty;
  uint64_t stat_rx_total;

  /* Congestion Control */
  uint64_t stat_cc_poll;
  uint64_t stat_cc_empty;
  uint64_t stat_cc_total;

  /* Application */
  uint64_t stat_ax_poll;
  uint64_t stat_ax_empty;
  uint64_t stat_ax_total;

  /* Application context */
  uint64_t stat_ac_poll;
  uint64_t stat_ac_empty;
  uint64_t stat_ac_total;

  /* Kernel Native Interface */
  uint64_t stat_kni_poll;
  uint64_t stat_kni_empty;
  uint64_t stat_kni_total;

  /* TCP States */
  uint64_t stat_tcp_poll;
  uint64_t stat_tcp_empty;
  uint64_t stat_tcp_total;

  /* Accept States */
  uint64_t stat_cyc_ta;
  uint64_t stat_cyc_la;
  uint64_t stat_cyc_kac;

  /* Kernel app_if task cycles */
  uint64_t stat_cyc_kclose;
  uint64_t stat_cyc_kmove;
  uint64_t stat_cyc_klopen;
  uint64_t stat_cyc_kreq_scale;

  /* Close sub task cycles */
  uint64_t stat_cyc_fs_lock;
  uint64_t stat_cyc_flow_slot_clear;
  uint64_t stat_cyc_tcp_close;
  uint64_t stat_cyc_conn_close_iter;
  uint64_t stat_conn_close_cnt;
  uint64_t stat_cyc_cc_remove;
  uint64_t stat_cyc_timeout_arm;

  /* Cycles consumed in processing by modules */
  uint64_t stat_cyc_rx;
  uint64_t stat_cyc_cc;
  uint64_t stat_cyc_ax;
  uint64_t stat_cyc_ac;
  uint64_t stat_cyc_kni;
  uint64_t stat_cyc_tcp;

  /* Fastpath -> Slowpath queuing delays */
  uint64_t stat_kout_cycles;
  uint64_t stat_kout_count;

  /* Lib -> Slowpath queueing delays */
  uint64_t stat_appin_cycles;
  uint64_t stat_appin_count;
#endif
};

#endif /* TASSTATS_H_ */
