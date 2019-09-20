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
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <rte_version.h>
#include <rte_kni.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#include <tas.h>
#include "internal.h"

#define MBUF_SIZE (1500  + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define POOL_SIZE (4 * 4096)
#define KNI_MTU 1500

enum change_linkstate {
  LST_NOOP = 0,
  LST_UP,
  LST_DOWN,
};

static int interface_set_carrier(const char *name, int status);
static int op_config_network_if(uint16_t port_id, uint8_t if_up);
#if RTE_VER_YEAR >= 18
static int op_config_mac_address(uint16_t port_id, uint8_t mac_addr[]);
#endif

static struct rte_mempool *kni_pool;
static struct rte_kni *kni_if;
static struct rte_kni_conf conf;
static struct rte_kni_ops ops = {
    .port_id = 0,
    .config_network_if = op_config_network_if,
#if RTE_VER_YEAR >= 18
    .config_mac_address = op_config_mac_address,
#endif
  };
static int change_linkstate = LST_NOOP;

int kni_init(void)
{
  if (config.kni_name == NULL)
    return 0;

#if RTE_VER_YEAR < 19
  rte_kni_init(1);
#else
  if (rte_kni_init(1) != 0) {
    fprintf(stderr, "kni_init: rte_kni_init failed\n");
    return -1;
  }
#endif

  /* alloc mempool for kni */
  kni_pool = rte_mempool_create("tas_kni", POOL_SIZE, MBUF_SIZE, 32,
      sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
      rte_pktmbuf_init, NULL, rte_socket_id(), 0);
  if (kni_pool == NULL) {
    fprintf(stderr, "kni_init: rte_mempool_create failed\n");
    return -1;
  }

  /* initialize config */
  memset(&conf, 0, sizeof(conf));
  strncpy(conf.name, config.kni_name, RTE_KNI_NAMESIZE - 1);
  conf.name[RTE_KNI_NAMESIZE - 1] = 0;
  conf.mbuf_size = MBUF_SIZE;
#if RTE_VER_YEAR >= 18
  memcpy(conf.mac_addr, &eth_addr, sizeof(eth_addr));
  conf.mtu = KNI_MTU;
#endif

  /* allocate kni */
  if ((kni_if = rte_kni_alloc(kni_pool, &conf, &ops)) == NULL) {
    fprintf(stderr, "kni_init: rte_kni_alloc failed\n");
    return -1;
  }

  return 0;
}

void kni_packet(const void *pkt, uint16_t len)
{
  struct rte_mbuf *mb;
  void *dst;

  if (config.kni_name == NULL)
    return;

  if ((mb =  rte_pktmbuf_alloc(kni_pool)) == NULL) {
    fprintf(stderr, "kni_packet: mbuf alloc failed\n");
    return;
  }

  if ((dst = rte_pktmbuf_append(mb, len)) == NULL) {
    fprintf(stderr, "kni_packet: mbuf append failed\n");
    return;
  }

  memcpy(dst, pkt, len);
  if (rte_kni_tx_burst(kni_if, &mb, 1) != 1) {
    fprintf(stderr, "kni_packet: send failed\n");
    rte_pktmbuf_free(mb);
  }

}

unsigned kni_poll(void)
{
  unsigned n;
  struct rte_mbuf *mb;
  uint32_t op;
  void *buf;

  if (config.kni_name == NULL)
    return 0;

  if (change_linkstate != LST_NOOP) {
    if (interface_set_carrier(config.kni_name, change_linkstate == LST_UP)) {
      fprintf(stderr, "kni_poll: linkstate update failed\n");
    }
    change_linkstate = LST_NOOP;
  }

  rte_kni_handle_request(kni_if);

  n = rte_kni_rx_burst(kni_if, &mb, 1);
  if (n == 1) {
    if (nicif_tx_alloc(rte_pktmbuf_pkt_len(mb), &buf, &op) == 0) {
      memcpy(buf, rte_pktmbuf_mtod(mb, void *), rte_pktmbuf_pkt_len(mb));
      nicif_tx_send(op, 1);
    } else {
      fprintf(stderr, "kni_poll: send failed\n");
    }

    rte_pktmbuf_free(mb);
  }
  return 1;
}

static int interface_set_carrier(const char *name, int status)
{
  char path[64];
  int fd, ret;
  const char *st = status ? "1" : "0";

  sprintf(path, "/sys/class/net/%s/carrier", name);

  if ((fd = open(path, O_WRONLY)) < 0) {
    perror("interface_set_carrier: open failed");
    return -1;
  }

  if ((ret = write(fd, st, 2)) != 2) {
    perror("interface_set_carrier: write failed");
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int op_config_network_if(uint16_t port_id, uint8_t if_up)
{
  change_linkstate = (if_up ? LST_UP : LST_DOWN);
  return 0;
}

#if RTE_VER_YEAR >= 18
static int op_config_mac_address(uint16_t port_id, uint8_t mac_addr[])
{
  return 0;
}
#endif
