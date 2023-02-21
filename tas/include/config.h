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

#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>

/** Supported congestion control algorithms. */
enum config_cc_algorithm {
  /** Window-based DCTCP */
  CONFIG_CC_DCTCP_WIN,
  /** Rate-based DCTCP */
  CONFIG_CC_DCTCP_RATE,
  /** TIMELY */
  CONFIG_CC_TIMELY,
  /** Constant connection rate */
  CONFIG_CC_CONST_RATE,
};

/** Struct containing the parsed configuration parameters */
struct configuration {
  /* shared memory size */
  uint64_t shm_len;
  /** Kernel nic receive queue length. */
  uint64_t nic_rx_len;
  /** Kernel nic transmit queue length. */
  uint64_t nic_tx_len;
  /** App context -> kernel queue length. */
  uint64_t app_kin_len;
  /** App context <- kernel queue length. */
  uint64_t app_kout_len;
  /** TCP receive buffer size. */
  uint64_t tcp_rxbuf_len;
  /** TCP transmit buffer size. */
  uint64_t tcp_txbuf_len;
  /** Initial tcp rtt for cc rate [us]*/
  uint32_t tcp_rtt_init;
  /** Link bandwidth for converting window to rate [gbps] */
  uint32_t tcp_link_bw;
  /** Initial tcp handshake timeout [us] */
  uint32_t tcp_handshake_to;
  /** # of retries for dropped handshake packets */
  uint32_t tcp_handshake_retries;
  /** Window scale configuration */
  uint8_t tcp_window_scale;
  /** IP address for this host */
  uint32_t ip;
  /** IP prefix length for this host */
  uint8_t ip_prefix;
  /** List of routes */
  struct config_route *routes;
  /** Initial ARP timeout in [us] */
  uint32_t arp_to;
  /** Maximum ARP timeout [us] */
  uint32_t arp_to_max;
  /** Congestion control algorithm */
  enum config_cc_algorithm cc_algorithm;
  /** CC: minimum delay between running control loop [us] */
  uint32_t cc_control_granularity;
  /** CC: control interval (multiples of conn RTT) */
  uint32_t cc_control_interval;
  /** CC: number of intervals without ACKs before retransmit */
  uint32_t cc_rexmit_ints;
  /** CC dctcp: EWMA weight for new ECN */
  uint32_t cc_dctcp_weight;
  /** CC dctcp: initial rate [kbps] */
  uint32_t cc_dctcp_init;
  /** CC dctcp: additive increase step [kbps] */
  uint32_t cc_dctcp_step;
  /** CC dctcp: multiplicative increase */
  uint32_t cc_dctcp_mimd;
  /** CC dctcp: minimal rate */
  uint32_t cc_dctcp_min;
  /** CC dctcp: min number of packets to wait for */
  uint32_t cc_dctcp_minpkts;
  /** CC Const: Rate to assign to flows [kbps] */
  uint32_t cc_const_rate;
  /** CC timely: low threshold */
  uint32_t cc_timely_tlow;
  /** CC timely: high threshold */
  uint32_t cc_timely_thigh;
  /** CC timely: additive increment step [kbps] */
  uint32_t cc_timely_step;
  /** CC timely: initial rate [kbps] */
  uint32_t cc_timely_init;
  /** CC timely: ewma weight for rtt diff */
  uint32_t cc_timely_alpha;
  /** CC timely: multiplicative decrement factor */
  uint32_t cc_timely_beta;
  /** CC timely: minimal RTT without queuing */
  uint32_t cc_timely_min_rtt;
  /** CC timely: minimal rate to use */
  uint32_t cc_timely_min_rate;
  /** FP: maximal number of cores used */
  uint32_t fp_cores_max;
  /** FP: interrupts (blocking) enabled */
  uint32_t fp_interrupts;
  /** FP: tcp checksum offload enabled */
  uint32_t fp_xsumoffload;
  /** FP: auto scaling enabled */
  uint32_t fp_autoscale;
  /** FP: use huge pages for internal and buffer memory */
  uint32_t fp_hugepages;
  /** FP: enable vlan stripping */
  uint32_t fp_vlan_strip;
  /** FP: polling interval for TAS */
  uint32_t fp_poll_interval_tas;
  /** FP: polling interval for app */
  uint32_t fp_poll_interval_app;
  /** SP: kni interface name */
  char *kni_name;
  /** Ready signal fd */
  int ready_fd;
  /** Minimize output */
  int quiet;
  /** DPDK extra argument vector */
  char **dpdk_argv;
  /** DPDK extra argument count */
  int dpdk_argc;
};

/** Route entry in configuration */
struct config_route {
  /** Destination IP address */
  uint32_t ip;
  /** Destination prefix length */
  uint8_t ip_prefix;
  /** Next hop IP */
  uint32_t next_hop_ip;
  /** Next pointer for route list */
  struct config_route *next;
};

/**
 * Parse command line parameters to fill in configuration struct.
 *
 * @param c    Config struct to store parameters in.
 * @param argc Argument count.
 * @param argv Argument vector.
 *
 * @return 0 on success, != 0.
 */
int config_parse(struct configuration *c, int argc, char *argv[]);

#endif /* ndef CONFIG_H_ */
