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

#include <utils.h>
#include <tas.h>
#include "internal.h"

static void timeout_trigger(struct timeout *to, uint8_t type, void *opaque);
static void signal_tas_ready(void);
void flexnic_loadmon(uint32_t cur_ts);

struct timeout_manager timeout_mgr;
static int exited = 0;
struct kernel_statistics kstats;
uint32_t cur_ts;
static uint32_t startwait = 0;
int kernel_notifyfd = 0;

int slowpath_main(void)
{
  uint32_t last_print = 0;
  uint32_t loadmon_ts = 0;

  kernel_notifyfd = eventfd(0, 0);
  assert(kernel_notifyfd != -1);

  struct epoll_event ev = {
    .events = EPOLLIN,
    .data.fd = kernel_notifyfd,
  };

  int epfd = epoll_create1(0);
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

  signal_tas_ready();

  while (exited == 0) {
    unsigned n = 0;

    cur_ts = util_timeout_time_us();
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

    if(UNLIKELY(n == 0)) {
      if(startwait == 0) {
	startwait = cur_ts;
      } else if(cur_ts - startwait >= POLL_CYCLE) {
	// Idle -- wait for data from apps/flexnic
	uint32_t cc_timeout = cc_next_ts(cur_ts),
	  util_timeout = util_timeout_next(&timeout_mgr, cur_ts),
	  timeout_us;
	int timeout_ms;

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

	/* fprintf(stderr, "idle - timeout %d ms, cc_timeout = %u us, util_timeout = %u us\n", timeout_ms, cc_timeout, util_timeout); */
	struct epoll_event event[2];
	int n;
      again:
	n = epoll_wait(epfd, event, 2, timeout_ms);
	if(n == -1) {
	  if(errno == EINTR) {
	    // XXX: To support attaching GDB
	    goto again;
	  }
	}
	assert(n != -1);
	/* fprintf(stderr, "busy - %u events\n", n); */
	for(int i = 0; i < n; i++) {
	  assert(event[i].data.fd == kernel_notifyfd);
	  uint64_t val;
	  /* fprintf(stderr, "- woken up by event FD = %d\n", event[i].data.fd); */
	  int r = read(kernel_notifyfd, &val, sizeof(uint64_t));
	  assert(r == sizeof(uint64_t));
	}
      }
    } else {
      startwait = 0;
    }

    if (cur_ts - last_print >= 1000000) {
      if (!config.quiet) {
        printf("stats: drops=%"PRIu64" k_rexmit=%"PRIu64" ecn=%"PRIu64" acks=%"
            PRIu64"\n", kstats.drops, kstats.kernel_rexmit, kstats.ecn_marked,
            kstats.acks);
        fflush(stdout);
      }
      last_print = cur_ts;
    }
  }

  return EXIT_SUCCESS;
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
