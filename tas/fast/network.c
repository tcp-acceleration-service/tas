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
#include <assert.h>

#include <rte_config.h>
#include <rte_memcpy.h>
#include <rte_malloc.h>
#include <rte_lcore.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_version.h>
#include <rte_spinlock.h>

#include <utils.h>
#include <utils_rng.h>
#include <tas_memif.h>
#include "internal.h"

#define PERTHREAD_MBUFS 2048
#define MBUF_SIZE (BUFFER_SIZE + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define RX_DESCRIPTORS 256
#define TX_DESCRIPTORS 128

uint8_t net_port_id = 0;
static struct rte_eth_conf port_conf = {
    .rxmode = {
      .mq_mode = ETH_MQ_RX_RSS,
      .offloads = 0,
#if RTE_VER_YEAR < 18
      .ignore_offload_bitfield = 1,
#endif
    },
    .txmode = {
      .mq_mode = ETH_MQ_TX_NONE,
      .offloads = 0,
    },
    .rx_adv_conf = {
      .rss_conf = {
        .rss_hf = ETH_RSS_NONFRAG_IPV4_TCP,
      },
    },
    .intr_conf = {
      .rxq = 1,
    },
  };

static unsigned num_threads;
static struct network_rx_thread **net_threads;

static struct rte_eth_dev_info eth_devinfo;
#if RTE_VER_YEAR < 19
  struct ether_addr eth_addr;
#else
  struct rte_ether_addr eth_addr;
#endif

uint16_t rss_reta_size;
static struct rte_eth_rss_reta_entry64 *rss_reta = NULL;
static uint16_t *rss_core_buckets = NULL;

static struct rte_mempool *mempool_alloc(void);
static int reta_setup(void);
static int reta_mlx5_resize(void);
static rte_spinlock_t initlock = RTE_SPINLOCK_INITIALIZER;

int network_init(unsigned n_threads)
{
  uint8_t count;
  int ret;
  uint16_t p;

  num_threads = n_threads;

  /* allocate thread pointer arrays */
  net_threads = rte_calloc("net thread ptrs", n_threads, sizeof(*net_threads), 0);
  if (net_threads == NULL) {
    goto error_exit;
  }

  /* make sure there is only one port */
#if RTE_VER_YEAR < 18
  count = rte_eth_dev_count();
#else
  count = rte_eth_dev_count_avail();
#endif
  if (count == 0) {
    fprintf(stderr, "No ethernet devices\n");
    goto error_exit;
  } else if (count > 1) {
    fprintf(stderr, "Multiple ethernet devices\n");
    goto error_exit;
  }

  RTE_ETH_FOREACH_DEV(p) {
    net_port_id = p;
  }

  /* get mac address and device info */
  rte_eth_macaddr_get(net_port_id, &eth_addr);
  rte_eth_dev_info_get(net_port_id, &eth_devinfo);

  if (eth_devinfo.max_rx_queues < n_threads ||
      eth_devinfo.max_tx_queues < n_threads)
  {
    fprintf(stderr, "Error: NIC does not support enough hw queues (rx=%u tx=%u)"
        " for the requested number of cores (%u)\n", eth_devinfo.max_rx_queues,
        eth_devinfo.max_tx_queues, n_threads);
    goto error_exit;
  }

  /* mask unsupported RSS hash functions */
  if ((port_conf.rx_adv_conf.rss_conf.rss_hf &
       eth_devinfo.flow_type_rss_offloads) !=
      port_conf.rx_adv_conf.rss_conf.rss_hf)
  {
    fprintf(stderr, "Warning: NIC does not support all requested RSS "
        "hash functions.\n");
    port_conf.rx_adv_conf.rss_conf.rss_hf &= eth_devinfo.flow_type_rss_offloads;
  }

  /* enable per port checksum offload if requested */
  if (config.fp_xsumoffload)
    port_conf.txmode.offloads =
      DEV_TX_OFFLOAD_IPV4_CKSUM | DEV_TX_OFFLOAD_TCP_CKSUM;

  /* disable rx interrupts if requested */
  if (!config.fp_interrupts)
    port_conf.intr_conf.rxq = 0;

  /* initialize port */
  ret = rte_eth_dev_configure(net_port_id, n_threads, n_threads, &port_conf);
  if (ret < 0) {
    fprintf(stderr, "rte_eth_dev_configure failed\n");
    goto error_exit;
  }

  /* workaround for mlx5. */
  if (config.fp_autoscale) {
    if (reta_mlx5_resize() != 0) {
      goto error_exit;
    }
  }

#if RTE_VER_YEAR < 18
  eth_devinfo.default_txconf.txq_flags = ETH_TXQ_FLAGS_IGNORE;
#endif
  eth_devinfo.default_rxconf.offloads = 0;

  /* enable per-queue checksum offload if requested */
  eth_devinfo.default_txconf.offloads = 0;
  if (config.fp_xsumoffload)
    eth_devinfo.default_txconf.offloads =
      DEV_TX_OFFLOAD_IPV4_CKSUM | DEV_TX_OFFLOAD_TCP_CKSUM;

  memcpy(&tas_info->mac_address, &eth_addr, 6);

  return 0;

error_exit:
  rte_free(net_threads);
  return -1;
}

void network_cleanup(void)
{
  rte_eth_dev_stop(net_port_id);
  rte_free(net_threads);
}

void network_dump_stats(void)
{
  struct rte_eth_stats stats;
  if (rte_eth_stats_get(0, &stats) == 0) {
    fprintf(stderr, "network stats: ipackets=%"PRIu64" opackets=%"PRIu64
        " ibytes=%"PRIu64" obytes=%"PRIu64" imissed=%"PRIu64" ierrors=%"PRIu64
        " oerrors=%"PRIu64" rx_nombuf=%"PRIu64"\n", stats.ipackets,
        stats.opackets, stats.ibytes, stats.obytes, stats.imissed,
        stats.ierrors, stats.oerrors, stats.rx_nombuf);
  } else {
    fprintf(stderr, "failed to get stats\n");
  }
}

int network_thread_init(struct dataplane_context *ctx)
{
  static volatile uint32_t tx_init_done = 0;
  static volatile uint32_t rx_init_done = 0;
  static volatile uint32_t start_done = 0;

  struct network_thread *t = &ctx->net;
  int ret;

  /* allocate mempool */
  if ((t->pool = mempool_alloc()) == NULL) {
    goto error_mpool;
  }

  /* initialize tx queue */
  t->queue_id = ctx->id;
  rte_spinlock_lock(&initlock);
  ret = rte_eth_tx_queue_setup(net_port_id, t->queue_id, TX_DESCRIPTORS,
          rte_socket_id(), &eth_devinfo.default_txconf);
  rte_spinlock_unlock(&initlock);
  if (ret != 0) {
    fprintf(stderr, "network_thread_init: rte_eth_tx_queue_setup failed\n");
    goto error_tx_queue;
  }

  /* barrier to make sure tx queues are initialized first */
  __sync_add_and_fetch(&tx_init_done, 1);
  while (tx_init_done < num_threads);

  /* initialize rx queue */
  t->queue_id = ctx->id;
  rte_spinlock_lock(&initlock);
  ret = rte_eth_rx_queue_setup(net_port_id, t->queue_id, RX_DESCRIPTORS,
          rte_socket_id(), &eth_devinfo.default_rxconf, t->pool);
  rte_spinlock_unlock(&initlock);
  if (ret != 0) {
    fprintf(stderr, "network_thread_init: rte_eth_rx_queue_setup failed\n");
    goto error_rx_queue;
  }

  /* barrier to make sure rx queues are initialized first */
  __sync_add_and_fetch(&rx_init_done, 1);
  while (rx_init_done < num_threads);

  /* start device if this ìs core 0 */
  if (ctx->id == 0) {
    if (rte_eth_dev_start(net_port_id) != 0) {
      fprintf(stderr, "rte_eth_dev_start failed\n");
      goto error_tx_queue;
    }

    /* enable vlan stripping if configured */
    if (config.fp_vlan_strip) {
      ret = rte_eth_dev_get_vlan_offload(net_port_id);
      ret |= ETH_VLAN_STRIP_OFFLOAD;
      if (rte_eth_dev_set_vlan_offload(net_port_id, ret)) {
        fprintf(stderr, "network_thread_init: vlan off set failed\n");
        goto error_tx_queue;
      }
    }

    /* setting up RETA failed */
    if (reta_setup() != 0) {
      fprintf(stderr, "RETA setup failed\n");
      goto error_tx_queue;
    }
    start_done = 1;
  }

  /* barrier wait for main thread to start the device */
  while (!start_done);

  if (config.fp_interrupts) {
    /* setup rx queue interrupt */
    rte_spinlock_lock(&initlock);
    ret = rte_eth_dev_rx_intr_ctl_q(net_port_id, t->queue_id,
        RTE_EPOLL_PER_THREAD, RTE_INTR_EVENT_ADD, NULL);
    rte_spinlock_unlock(&initlock);
    if (ret != 0) {
      fprintf(stderr, "network_thread_init: rte_eth_dev_rx_intr_ctl_q failed "
          "(%d)\n", rte_errno);
      goto error_int_queue;
    }
  }

  return 0;

error_int_queue:
  /* TODO: destroy rx queue */
error_rx_queue:
  /* TODO: destroy tx queue */
error_tx_queue:
  /* TODO: free mempool */
error_mpool:
  rte_free(t);
  return -1;
}

int network_rx_interrupt_ctl(struct network_thread *t, int turnon)
{
  if(turnon) {
    return rte_eth_dev_rx_intr_enable(net_port_id, t->queue_id);
  } else {
    return rte_eth_dev_rx_intr_disable(net_port_id, t->queue_id);
  }
}

static struct rte_mempool *mempool_alloc(void)
{
  static unsigned pool_id = 0;
  unsigned n;
  char name[32];
  n = __sync_fetch_and_add(&pool_id, 1);
  snprintf(name, 32, "mbuf_pool_%u\n", n);
  return rte_mempool_create(name, PERTHREAD_MBUFS, MBUF_SIZE, 32,
          sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
          rte_pktmbuf_init, NULL, rte_socket_id(), 0);

}

static inline uint16_t core_min(uint16_t num)
{
  uint16_t i, i_min = 0, v_min = UINT8_MAX;

  for (i = 0; i < num; i++) {
    if (rss_core_buckets[i] < v_min) {
      v_min = rss_core_buckets[i];
      i_min = i;
    }
  }

  return i_min;
}

static inline uint16_t core_max(uint16_t num)
{
  uint16_t i, i_max = 0, v_max = 0;

  for (i = 0; i < num; i++) {
    if (rss_core_buckets[i] >= v_max) {
      v_max = rss_core_buckets[i];
      i_max = i;
    }
  }

  return i_max;
}

int network_scale_up(uint16_t old, uint16_t new)
{
  uint16_t i, j, k, c, share = rss_reta_size / new;
  uint16_t outer, inner;

  /* clear mask */
  for (k = 0; k < rss_reta_size; k += RTE_RETA_GROUP_SIZE) {
    rss_reta[k / RTE_RETA_GROUP_SIZE].mask = 0;
  }

  k = 0;
  for (j = old; j < new; j++) {
    for (i = 0; i < share; i++) {
      c = core_max(old);

      for (; ; k = (k + 1) % rss_reta_size) {
        outer = k / RTE_RETA_GROUP_SIZE;
        inner = k % RTE_RETA_GROUP_SIZE;
        if (rss_reta[outer].reta[inner] == c) {
          rss_reta[outer].mask |= 1ULL << inner;
          rss_reta[outer].reta[inner] = j;
          fp_state->flow_group_steering[k] = j;
          break;
        }
      }

      rss_core_buckets[c]--;
      rss_core_buckets[j]++;
    }
  }

  if (rte_eth_dev_rss_reta_update(net_port_id, rss_reta, rss_reta_size) != 0) {
    fprintf(stderr, "network_scale_up: rte_eth_dev_rss_reta_update failed\n");
    return -1;
  }

  return 0;
}

int network_scale_down(uint16_t old, uint16_t new)
{
  uint16_t i, o_c, n_c, outer, inner;

  /* clear mask */
  for (i = 0; i < rss_reta_size; i += RTE_RETA_GROUP_SIZE) {
    rss_reta[i / RTE_RETA_GROUP_SIZE].mask = 0;
  }

  for (i = 0; i < rss_reta_size; i++) {
    outer = i / RTE_RETA_GROUP_SIZE;
    inner = i % RTE_RETA_GROUP_SIZE;

    o_c = rss_reta[outer].reta[inner];
    if (o_c >= new) {
      n_c = core_min(new);

      rss_reta[outer].reta[inner] = n_c;
      rss_reta[outer].mask |= 1ULL << inner;

      fp_state->flow_group_steering[i] = n_c;

      rss_core_buckets[o_c]--;
      rss_core_buckets[n_c]++;
    }
  }

  if (rte_eth_dev_rss_reta_update(net_port_id, rss_reta, rss_reta_size) != 0) {
    fprintf(stderr, "network_scale_down: rte_eth_dev_rss_reta_update failed\n");
    return -1;
  }

  return 0;
}

static int reta_setup()
{
  uint16_t i, c;

  /* allocate RSS redirection table and core-bucket count table */
  rss_reta_size = eth_devinfo.reta_size;
  if (rss_reta_size == 0) {
    fprintf(stderr, "Warning: NIC does not expose reta size\n");
    rss_reta_size = rte_align32pow2(fp_cores_cur); /* map groups to fp cores in this case */
  }
  if (rss_reta_size > FLEXNIC_PL_MAX_FLOWGROUPS) {
    fprintf(stderr, "reta_setup: reta size (%u) greater than maximum supported"
        " (%u)\n", rss_reta_size, FLEXNIC_PL_MAX_FLOWGROUPS);
    abort();
  }
  if (!rte_is_power_of_2(rss_reta_size)) {
    fprintf(stderr, "reta_setup: reta size (%u) is not a power of 2\n", rss_reta_size);
    abort();
  }

  rss_reta = rte_calloc("rss reta", ((rss_reta_size + RTE_RETA_GROUP_SIZE - 1) /
        RTE_RETA_GROUP_SIZE), sizeof(*rss_reta), 0);
  rss_core_buckets = rte_calloc("rss core buckets", fp_cores_max,
      sizeof(*rss_core_buckets), 0);

  if (rss_reta == NULL || rss_core_buckets == NULL) {
    fprintf(stderr, "reta_setup: rss_reta alloc failed\n");
    goto error_exit;
  }

  /* initialize reta */
  for (i = 0, c = 0; i < rss_reta_size; i++) {
    rss_core_buckets[c]++;
    rss_reta[i / RTE_RETA_GROUP_SIZE].mask = -1ULL;
    rss_reta[i / RTE_RETA_GROUP_SIZE].reta[i % RTE_RETA_GROUP_SIZE] = c;
    fp_state->flow_group_steering[i] = c;
    c = (c + 1) % fp_cores_cur;
  }

  if (rte_eth_dev_rss_reta_update(net_port_id, rss_reta, rss_reta_size) != 0 && config.fp_autoscale) {
    fprintf(stderr, "reta_setup: rte_eth_dev_rss_reta_update failed\n");
    goto error_exit;
  }

  return 0;

error_exit:
  rte_free(rss_core_buckets);
  rte_free(rss_reta);
  return -1;
}

/* The mlx5 driver by default picks reta size = number of queues. Which is not
 * enough for scaling up and down with balanced load. But when updating the reta
 * with a larger size, the mlx5 driver resizes the reta.
 */
static int reta_mlx5_resize(void)
{
  if (!strcmp(eth_devinfo.driver_name, "net_mlx5")) {
    /* for mlx5 we can increase the size with a call to
     * rte_eth_dev_rss_reta_update with the target size, so just up the
     * reta_sizeo in devinfo so that the reta_setup() call increases it.
     */
    eth_devinfo.reta_size = 512;
  }

  /* warn if reta is too small */
  if (eth_devinfo.reta_size < 128) {
    fprintf(stderr, "net: RSS redirection table is small (%u), this results in"
        " bad load balancing when scaling down\n", eth_devinfo.reta_size);
  }

  return 0;
}
