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

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <utils.h>
#include <tas.h>
#include "internal.h"

static void slowpath_block(uint32_t cur_ts);
static void timeout_trigger(struct timeout *to, uint8_t type, void *opaque);
static void signal_tas_ready(void);
void flexnic_loadmon(uint32_t cur_ts);

static void init_vm_weights(double *vm_weights);
int64_t boost_budget(int vmid, int ctxid, int64_t incr, 
    int64_t *last_bu_used);
void redistr_unused_budget(int vmid, int ctxid, int64_t incr);
static void update_budget(int threads_launched, 
    int64_t bu_stats[FLEXNIC_PL_VMST_NUM][1]);
uint64_t get_total_cycles_consumed(int vmid);
uint64_t get_round_cycles_consumed(int vmid);
uint64_t get_poll_cycles_consumed(int vmid);
uint64_t get_rx_cycles_consumed(int vmid);
uint64_t get_tx_cycles_consumed(int vmid);
    
struct timeout_manager timeout_mgr;
static int exited = 0;
struct kernel_statistics kstats;
uint32_t cur_ts;
int kernel_notifyfd = 0;
int vm_weights_sum;
static int epfd;

double vm_weights[FLEXNIC_PL_VMST_NUM - 1];
uint64_t last_bu_update_ts = 0;
int64_t last_bu_update_incr[FLEXNIC_PL_VMST_NUM][FLEXNIC_PL_APPST_CTX_NUM];

int slowpath_main(int threads_launched)
{
  int64_t bu_stats[FLEXNIC_PL_VMST_NUM][1];
  memset(bu_stats, 0, sizeof(int64_t) * FLEXNIC_PL_VMST_NUM * threads_launched);
  struct notify_blockstate nbs;
  uint32_t last_print = 0;
  uint32_t last_baccum = 0;
  uint32_t loadmon_ts = 0;
  uint64_t cycs_ts;
  uint64_t last_cycs_ts = util_rdtsc();

  kernel_notifyfd = eventfd(0, EFD_NONBLOCK);
  assert(kernel_notifyfd != -1);

  struct epoll_event ev = {
    .events = EPOLLIN,
    .data.fd = kernel_notifyfd,
  };

  epfd = epoll_create1(0);
  assert(epfd != -1);

  int r = epoll_ctl(epfd, EPOLL_CTL_ADD, kernel_notifyfd, &ev);
  assert(r == 0);

  /* initialize timers for timeouts */
  if (util_timeout_init(&timeout_mgr, timeout_trigger, NULL)) {
    fprintf(stderr, "timeout_init failed\n");
    return EXIT_FAILURE;
  }

  /* initialize kni */
  if (kni_init()) {
    fprintf(stderr, "kni_init failed\n");
    return EXIT_FAILURE;
  }

  /* initialize routing subsystem */
  if (routing_init()) {
    fprintf(stderr, "routing_init failed\n");
    return EXIT_FAILURE;
  }

  /* connect to NIC */
  if (nicif_init()) {
    fprintf(stderr, "nicif_init failed\n");
    return EXIT_FAILURE;
  }

  /* initialize CC */
  if (cc_init()) {
    fprintf(stderr, "cc_init failed\n");
    return EXIT_FAILURE;
  }

  /* prepare application interface */
  if (appif_init()) {
    fprintf(stderr, "appif_init failed\n");
    return EXIT_FAILURE;
  }

  if (arp_init()) {
    fprintf(stderr, "arp_init failed\n");
    return EXIT_FAILURE;
  }


  if (tcp_init()) {
    fprintf(stderr, "tcp_init failed\n");
    return EXIT_FAILURE;
  }

  init_vm_weights(vm_weights);
  memset(last_bu_update_incr, 0, sizeof(last_bu_update_incr));

  signal_tas_ready();

  notify_canblock_reset(&nbs);
  while (exited == 0) {
    unsigned n = 0;
    cur_ts = util_timeout_time_us();
    
    /* Accumulate per context VM budget and update total budget */
    if (cur_ts - last_baccum >= config.bu_update_freq) {
      update_budget(threads_launched, bu_stats);
      last_baccum = cur_ts;
    }

    n += nicif_poll();
    n += cc_poll(cur_ts);
    n += appif_poll();
    n += kni_poll();
    tcp_poll();
    util_timeout_poll_ts(&timeout_mgr, cur_ts);

    if (config.fp_autoscale && cur_ts - loadmon_ts >= 10000) {
      flexnic_loadmon(cur_ts);
      loadmon_ts = cur_ts;
    }

    if (notify_canblock(&nbs, n != 0, util_rdtsc())) {
      slowpath_block(cur_ts);
      notify_canblock_reset(&nbs);
    }

    if (cur_ts - last_print >= 1000000) {
      cycs_ts = util_rdtsc();
      if (!config.quiet) {
        printf("stats: drops=%"PRIu64" k_rexmit=%"PRIu64" ecn=%"PRIu64" acks=%"PRIu64"\n",
            kstats.drops, kstats.kernel_rexmit, kstats.ecn_marked, kstats.acks);
        printf("ts=%ld elapsed=%ld " 
            "TVM0=%ld TVM1=%ld " 
            "RVM0=%ld RVM1=%ld "
            "POLLVM0=%ld TXVM0=%ld RXVM0=%ld "
            "POLLVM1=%ld TXVM1=%ld RXVM1=%ld "
            "BUVM0=%ld BUVM1=%ld\n", cycs_ts, cycs_ts - last_cycs_ts, 
            get_total_cycles_consumed(0), get_total_cycles_consumed(1),
            get_round_cycles_consumed(0), get_round_cycles_consumed(1),
            get_poll_cycles_consumed(0), get_tx_cycles_consumed(0), get_rx_cycles_consumed(0),
            get_poll_cycles_consumed(1), get_tx_cycles_consumed(1), get_rx_cycles_consumed(1),
            bu_stats[0][0], bu_stats[1][0]);
        fflush(stdout);
      }
      last_print = cur_ts;
      last_cycs_ts = cycs_ts;
    }
  }

  return EXIT_SUCCESS;
}

static void init_vm_weights(double *vm_weights)
{
  int vmid;

  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM - 1; vmid++)
  {
    vm_weights[vmid] = 1;
    vm_weights_sum += 1;
  }
}

static void update_budget(int threads_launched, 
    int64_t bu_stats[FLEXNIC_PL_VMST_NUM][1])
{
  int vmid, ctxid;
  uint64_t cur_ts, bu_unused_div;
  int64_t total_budget, incr, last_incr, bu_unused, use_target;
  uint64_t bu_unused_sum[threads_launched];
  int64_t bu_used[FLEXNIC_PL_VMST_NUM][FLEXNIC_PL_APPCTX_NUM];

  memset(bu_unused_sum, 0, sizeof(bu_unused_sum));
  memset(bu_used, 0, sizeof(bu_used));

  cur_ts = util_rdtsc();
  total_budget = config.bu_boost * (cur_ts - last_bu_update_ts);

  /* Update budget */
  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM - 1; vmid++)
  {
    incr = ((total_budget * vm_weights[vmid]) / vm_weights_sum);
    
    for (ctxid = 0; ctxid < threads_launched; ctxid++)
    {
      last_incr = last_bu_update_incr[vmid][ctxid];
      last_bu_update_incr[vmid][ctxid] = incr;

      bu_stats[vmid][ctxid] = boost_budget(vmid, ctxid, 
          incr, &bu_used[vmid][ctxid]);

      bu_unused = last_incr - bu_used[vmid][ctxid];

      use_target = last_incr * config.bu_use_ratio;
      if ((use_target - bu_unused) < 0)
      {
        bu_unused_sum[ctxid] += bu_unused;
      }
    }
  }

  /* Reallocate unused cycles */
  for (ctxid = 0; ctxid < threads_launched; ctxid++)
  {
    bu_unused_div = bu_unused_sum[ctxid] / (FLEXNIC_PL_VMST_NUM - 1);
    for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM - 1; vmid++)
    {
      redistr_unused_budget(vmid, ctxid, bu_unused_div);
    }
  }

  last_bu_update_ts = cur_ts;
}

static void slowpath_block(uint32_t cur_ts)
{
  int n, i, ret, timeout_ms;
  struct epoll_event event[2];
  uint64_t val;
  uint32_t cc_timeout = cc_next_ts(cur_ts),
    util_timeout = util_timeout_next(&timeout_mgr, cur_ts),
    timeout_us;

  if(cc_timeout != -1U && util_timeout != -1U) {
    timeout_us = MIN(cc_timeout, util_timeout);
  } else if(cc_timeout != -1U) {
    timeout_us = util_timeout;
  } else {
    timeout_us = cc_timeout;
  }
  if(timeout_us != -1U) {
    timeout_ms = timeout_us / 1000;
  } else {
    timeout_ms = -1;
  }

  // Deal with load management
  if(timeout_ms == -1 || timeout_ms > 1000) {
    timeout_ms = 10;
  }

again:
  n = epoll_wait(epfd, event, 2, timeout_ms);
  if(n == -1 && errno == EINTR) {
    /* To support attaching GDB */
    goto again;
  } else if (n == -1) {
    perror("slowpath_block: epoll_wait failed");
    abort();
  }

  for(i = 0; i < n; i++) {
    assert(event[i].data.fd == kernel_notifyfd);
    ret = read(kernel_notifyfd, &val, sizeof(uint64_t));
    if ((ret > 0 && ret != sizeof(uint64_t)) ||
        (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
    {
      perror("slowpath_block: read failed");
      abort();
    }
  }
}

static void timeout_trigger(struct timeout *to, uint8_t type, void *opaque)
{
  switch (type) {
    case TO_ARP_REQ:
      arp_timeout(to, type);
      break;

    case TO_TCP_HANDSHAKE:
    case TO_TCP_RETRANSMIT:
    case TO_TCP_CLOSED:
      tcp_timeout(to, type);
      break;

    default:
      fprintf(stderr, "Unknown timeout type: %u\n", type);
      abort();
  }
}

static void signal_tas_ready(void)
{
  uint64_t x;

  printf("TAS ready\n");
  fflush(stdout);

  x = 1;
  if (config.ready_fd >= 0 &&
      write(config.ready_fd, &x, sizeof(x)) < 0)
  {
    perror("TAS signal: ready fd write failed");
    /* proceeed */
  }
}
