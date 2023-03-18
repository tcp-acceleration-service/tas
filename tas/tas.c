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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_launch.h>
#include <rte_cycles.h>
#include <rte_malloc.h>

#include <tas_memif.h>
#include <utils_timeout.h>
#include <utils_sync.h>

#include <tas.h>
#include <fastpath.h>
#include "fast/internal.h"
#include "slow/internal.h"

struct core_load {
  uint64_t cyc_busy;
};

struct configuration config;

unsigned fp_cores_max;
volatile unsigned fp_cores_cur = 1;
volatile unsigned fp_scale_to = 0;

static unsigned threads_launched = 0;
int exited;

struct budget_statistics budget_statistics;
struct dataplane_context **ctxs = NULL;
struct core_load *core_loads = NULL;

static int start_threads(void);
static void thread_error(void);
static int common_thread(void *arg);


static void *slowpath_thread(int threads_launched)
{
  slowpath_main(threads_launched);
  return NULL;
}

int main(int argc, char *argv[])
{
  struct vm_budget **budgets;
  int res = EXIT_SUCCESS;

  /* parse command line options */
  if (config_parse(&config, argc, argv) != 0) {
    res = EXIT_FAILURE;
    goto error_exit;
  }
  fp_cores_max = fp_cores_cur = config.fp_cores_max;

  /* allocate shared memory before dpdk grabs all huge pages */
  if (shm_preinit() != 0) {
    res = EXIT_FAILURE;
    goto error_exit;
  }

  /* initialize dpdk */
  rte_log_set_global_level(RTE_LOG_ERR);
  if (rte_eal_init(config.dpdk_argc, config.dpdk_argv) < 0) {
    fprintf(stderr, "dpdk init failed\n");
    goto error_exit;
  }

  if ((core_loads = calloc(fp_cores_max, sizeof(*core_loads))) == NULL) {
    res = EXIT_FAILURE;
    fprintf(stderr, "core loads alloc failed\n");
    goto error_exit;
  }

  if (shm_init(fp_cores_max) != 0) {
    res = EXIT_FAILURE;
    fprintf(stderr, "dma init failed\n");
    goto error_exit;
  }

  if (network_init(fp_cores_max) != 0) {
    res = EXIT_FAILURE;
    fprintf(stderr, "network init failed\n");
    goto error_shm_cleanup;
  }

  if (dataplane_init() != 0) {
    res = EXIT_FAILURE;
    fprintf(stderr, "dpinit failed\n");
    goto error_network_cleanup;
  }

  shm_set_ready();

  if ((threads_launched = start_threads()) < 0) {
    res = EXIT_FAILURE;
    goto error_dataplane_cleanup;
  }

  if ((budgets = malloc(sizeof(struct vm_budget *) * threads_launched)) 
      == NULL)
  {
    goto error_dataplane_cleanup;
  }

  /* Start kernel thread */
  slowpath_thread(threads_launched);

error_dataplane_cleanup:
  /* TODO */
error_network_cleanup:
  network_cleanup();
error_shm_cleanup:
  shm_cleanup();
error_exit:
  return res;
}

static int common_thread(void *arg)
{
  uint16_t id = (uintptr_t) arg;
  struct dataplane_context *ctx;

  {
    char name[17];
    snprintf(name, sizeof(name), "stcp-fp-%u", id);
    pthread_setname_np(pthread_self(), name);
  }

  /* Allocate fastpath core context */
  if ((ctx = rte_zmalloc("fastpath core context", sizeof(*ctx), 0)) == NULL) {
    fprintf(stderr, "Allocating fastpath core context failed\n");
    goto error_alloc;
  }
  ctxs[id] = ctx;
  ctx->id = id;


  /* initialize trace if enabled */
#ifdef FLEXNIC_TRACING
  if (trace_thread_init(id) != 0) {
    fprintf(stderr, "initializing trace failed\n");
    goto error_trace;
  }
#endif

  /* initialize data plane context */
  if (dataplane_context_init(ctx) != 0) {
    fprintf(stderr, "initializing data plane context\n");
    goto error_dpctx;
  }

  /* poll doorbells and network */
  dataplane_loop(ctx);

  dataplane_context_destroy(ctx);
  return 0;

error_dpctx:
#ifdef FLEXNIC_TRACING
error_trace:
#endif
  dataplane_context_destroy(ctx);
error_alloc:
  thread_error();
  return -1;
}

static int start_threads(void)
{
  unsigned cores_avail, cores_needed, core;
  void *arg;

  cores_avail = rte_lcore_count();
  /* fast path cores + one slow path core */
  cores_needed = fp_cores_max + 1;

  if ((ctxs = rte_calloc("context list", fp_cores_max, sizeof(*ctxs), 64)) == NULL) {
    perror("datplane_init: calloc failed");
    return -1;
  }

  /* check that we have enough cores */
  if (cores_avail < cores_needed) {
    fprintf(stderr, "Not enough cores: got %u need %u\n", cores_avail,
        cores_needed);
    return -1;
  }

  /* start common threads */
  RTE_LCORE_FOREACH_SLAVE(core) {
    if (threads_launched < fp_cores_max) {
      arg = (void *) (uintptr_t) threads_launched;
      if (rte_eal_remote_launch(common_thread, arg, core) != 0) {
	fprintf(stderr, "ERROR\n");
        return -1;
      }
      threads_launched++;
    }
  }

  return threads_launched;
}

static void thread_error(void)
{
  fprintf(stderr, "thread_error\n");
  abort();
}

int flexnic_scale_to(uint32_t cores)
{
  if (fp_scale_to != 0) {
    fprintf(stderr, "flexnic_scale_to: already scaling\n");
    return -1;
  }

  fp_scale_to = cores;

  notify_fastpath_core(0);
  return 0;
}

uint64_t sum_hist(uint64_t hist[16 + 1], int n)
{
  int i;
  uint64_t sum = 0;
  for (i = 0; i < n; i++)
  {
    sum += hist[i];
  }

  return sum;
}

struct budget_statistics get_budget_stats(int vmid, int ctxid)
{
  struct budget_statistics stats;

  /* Get stats for this logging round */
  stats.budget = ctxs[ctxid]->budgets[vmid].budget;
  stats.cycles_poll = ctxs[ctxid]->budgets[vmid].cycles_poll;
  stats.cycles_tx = ctxs[ctxid]->budgets[vmid].cycles_tx;
  stats.cycles_rx = ctxs[ctxid]->budgets[vmid].cycles_rx;
  memcpy(stats.hist_poll, ctxs[ctxid]->hist_poll, sizeof(ctxs[ctxid]->hist_poll));
  stats.poll_total = sum_hist(stats.hist_poll, 16 + 1);
  memcpy(stats.hist_tx, ctxs[ctxid]->hist_tx, sizeof(ctxs[ctxid]->hist_tx));
  stats.tx_total = sum_hist(stats.hist_tx, 16 + 1);
  memcpy(stats.hist_rx, ctxs[ctxid]->hist_rx, sizeof(ctxs[ctxid]->hist_rx));
  stats.rx_total = sum_hist(stats.hist_rx, 16 + 1);
  stats.cycles_total = stats.cycles_poll + stats.cycles_tx + stats.cycles_rx;

  /* Reset stats for this logging round (budget is not reset) */
  __sync_fetch_and_sub(&ctxs[ctxid]->budgets[vmid].cycles_poll,
      stats.cycles_poll);

  __sync_fetch_and_sub(&ctxs[ctxid]->budgets[vmid].cycles_tx,
      stats.cycles_tx);

  __sync_fetch_and_sub(&ctxs[ctxid]->budgets[vmid].cycles_rx,
      stats.cycles_rx);

  memset(ctxs[ctxid]->hist_poll, 0, sizeof(ctxs[ctxid]->hist_poll));
  memset(ctxs[ctxid]->hist_tx, 0, sizeof(ctxs[ctxid]->hist_tx));
  memset(ctxs[ctxid]->hist_rx, 0, sizeof(ctxs[ctxid]->hist_rx));

  return stats;
}

void boost_budget(int vmid, int ctxid, int64_t incr)
{
  uint64_t old_budget, new_budget, max_budget;

  old_budget = ctxs[ctxid]->budgets[vmid].budget;
  new_budget = old_budget + incr;
  max_budget = config.bu_max_budget;  

  /* TODO: Leftovers that would go over budget (new_budget - max_budget)
     give to other VMs that would not go over MAX */
  if (new_budget > max_budget)
  {
    incr = max_budget - old_budget;
  }
  __sync_fetch_and_add(&ctxs[ctxid]->budgets[vmid].budget, incr);
  printf("INCR=%ld NEW_BUDGET=%ld\n", incr, ctxs[ctxid]->budgets[vmid].budget);
}

void flexnic_loadmon(uint32_t ts)
{
  uint64_t cyc_busy = 0, x, tsc, cycles, id_cyc;
  unsigned i, num_cores;
  static uint64_t ewma_busy = 0, ewma_cycles = 0, last_tsc = 0, kdrops = 0;
  static int waiting = 1, waiting_n = 0, count = 0;

  num_cores = fp_cores_cur;

  /* sum up busy cycles from all cores */
  for (i = 0; i < num_cores; i++) {
    if (ctxs[i] == NULL)
      return;

    x = ctxs[i]->loadmon_cyc_busy;
    cyc_busy += x - core_loads[i].cyc_busy;
    core_loads[i].cyc_busy = x;

    kdrops += ctxs[i]->kernel_drop;
    ctxs[i]->kernel_drop = 0;
  }

  /* measure cpu cycles since last call */
  tsc = rte_get_tsc_cycles();
  if (last_tsc == 0) {
    last_tsc = tsc;
    return;
  }
  cycles = tsc - last_tsc;
  last_tsc = tsc;

  /* ewma for busy cycles and total cycles */
  ewma_busy = (7 * ewma_busy + cyc_busy) / 8;
  ewma_cycles = (7 * ewma_cycles + cycles) / 8;

  /* periodically print out staticstics */
  if (count++ % 100 == 0) {
    if (!config.quiet)
      fprintf(stderr, "flexnic_loadmon: status cores = %u   busy = %lu  "
          "cycles =%lu  kdrops=%lu\n", num_cores, ewma_busy, ewma_cycles,
          kdrops);
    kdrops = 0;
  }

  /* waiting period after scaling decsions */
  if (waiting && ++waiting_n < 10)
    return;

  /* calculate idle cycles */
  if (num_cores * ewma_cycles > ewma_busy) {
    id_cyc = num_cores * ewma_cycles - ewma_busy;
  } else {
    id_cyc = 0;
  }

  /* scale down if idle iterations more than 1.25 cores are idle */
  if (num_cores > 1 && id_cyc > ewma_cycles * 5 / 4) {
    if (!config.quiet)
      fprintf(stderr, "flexnic_loadmon: down cores = %u   idle_cyc = %lu  "
          "1.2 cores = %lu\n", num_cores, id_cyc, ewma_cycles * 5 / 4);
    flexnic_scale_to(num_cores - 1);
    waiting = 1;
    waiting_n = 0;
    return;
  }

  /* scale up if idle iterations less than .2 of a core */
  if (num_cores < fp_cores_max && id_cyc < ewma_cycles / 5) {
    if (!config.quiet)
      fprintf(stderr, "flexnic_loadmon: up cores = %u   idle_cyc = %lu  "
          "0.2 cores = %lu\n", num_cores, id_cyc,  ewma_cycles / 5);
    flexnic_scale_to(num_cores + 1);
    waiting = 1;
    waiting_n = 0;
    return;
  }
}
