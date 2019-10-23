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

#ifndef FLEXNIC_TRACE_H_
#define FLEXNIC_TRACE_H_

#include <stdint.h>

#define FLEXNIC_TRACE_NAME "flexnic_trace_%u"

#define FLEXNIC_TRACE_EV_RXPKT 1
#define FLEXNIC_TRACE_EV_TXPKT 2
#define FLEXNIC_TRACE_EV_DMARD 3
#define FLEXNIC_TRACE_EV_DMAWR 4
#define FLEXNIC_TRACE_EV_QMSET 6
#define FLEXNIC_TRACE_EV_QMEVT 7

struct flexnic_trace_header {
  volatile uint64_t end_last;
  uint64_t length;
} __attribute__((packed));

struct flexnic_trace_entry_head {
  uint64_t ts;
  uint32_t seq;
  uint16_t type;
  uint16_t length;
} __attribute__((packed));

struct flexnic_trace_entry_tail {
  uint16_t length;
} __attribute__((packed));

struct flexnic_trace_entry_dma {
  uint64_t addr;
  uint64_t len;
  uint8_t data[];
} __attribute__((packed));

struct flexnic_trace_entry_qman_set {
  uint32_t id;
  uint32_t rate;
  uint32_t avail;
  uint16_t max_chunk;
  uint8_t  opaque;
  uint8_t  flags;
} __attribute__((packed));

struct flexnic_trace_entry_qman_event {
  uint32_t id;
  uint16_t bytes;
  uint8_t  opaque;
  uint8_t  pad;
} __attribute__((packed));


#define FLEXNIC_PL_TREV_ADB       0x100
#define FLEXNIC_PL_TREV_ATX       0x101
#define FLEXNIC_PL_TREV_ARX       0x102
#define FLEXNIC_PL_TREV_RXFS      0x103
#define FLEXNIC_PL_TREV_TXACK     0x104
#define FLEXNIC_PL_TREV_TXSEG     0x105
#define FLEXNIC_PL_TREV_ACTXQMAN  0x106
#define FLEXNIC_PL_TREV_AFLOQMAN  0x107
#define FLEXNIC_PL_TREV_REXMIT    0x108

/** application tx queue entry */
struct flextcp_pl_trev_atx {
  uint32_t rx_bump;
  uint32_t tx_bump;
  uint32_t bump_seq_ent;
  uint32_t flags;

  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t flow_id;
  uint32_t db_id;

  uint32_t tx_next_pos;
  uint32_t tx_next_seq;
  uint32_t tx_avail_prev;
  uint32_t rx_next_pos;
  uint32_t rx_avail;
  uint32_t tx_len;
  uint32_t rx_len;
  uint32_t rx_remote_avail;
  uint32_t tx_sent;
  uint32_t bump_seq_flow;
} __attribute__((packed));

/** application rx queue entry */
struct flextcp_pl_trev_arx {
  uint64_t opaque;
  uint32_t rx_bump;
  uint32_t tx_bump;
  uint32_t rx_pos;
  uint32_t flags;

  uint32_t flow_id;
  uint32_t db_id;

  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;
} __attribute__((packed));

/** tcp flow state on receive */
struct flextcp_pl_trev_rxfs {
  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t flow_id;
  uint32_t flow_seq;
  uint32_t flow_ack;
  uint16_t flow_flags;
  uint16_t flow_len;

  uint32_t fs_rx_nextpos;
  uint32_t fs_rx_nextseq;
  uint32_t fs_rx_avail;
  uint32_t fs_tx_nextpos;
  uint32_t fs_tx_nextseq;
  uint32_t fs_tx_sent;
  uint32_t fs_tx_avail;
} __attribute__((packed));

/** tcp ack sent out */
struct flextcp_pl_trev_txack {
  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t flow_seq;
  uint32_t flow_ack;
  uint16_t flow_flags;
} __attribute__((packed));

/* tcp segment sent out */
struct flextcp_pl_trev_txseg {
  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t flow_seq;
  uint32_t flow_ack;
  uint16_t flow_flags;
  uint16_t flow_len;
} __attribute__((packed));

/* queue manager event fetching flow payload */
struct flextcp_pl_trev_afloqman {
  uint64_t tx_base;
  uint32_t tx_avail;
  uint32_t tx_next_pos;
  uint32_t tx_len;
  uint32_t rx_remote_avail;
  uint32_t tx_sent;

  uint32_t flow_id;
} __attribute__((packed));

/* reset flow re-transmission */
struct flextcp_pl_trev_rexmit {
  uint32_t flow_id;
  uint32_t tx_avail;
  uint32_t tx_sent;
  uint32_t tx_next_pos;
  uint32_t tx_next_seq;
  uint32_t rx_remote_avail;
} __attribute__((packed));




#endif
