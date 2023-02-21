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

#ifndef FLEXTCP_PLIF_H_
#define FLEXTCP_PLIF_H_

#include <stdint.h>
#include <utils.h>
#include <packet_defs.h>

/**
 * @addtogroup tas-fp
 * @brief TAS Fast Path
 * @ingroup tas
 * @{ */

#define FLEXNIC_HUGE_PREFIX "/dev/hugepages"

/** Name for the info shared memory region. */
#define FLEXNIC_NAME_INFO "tas_info"
/** Name for flexnic dma shared memory region. */
#define FLEXNIC_NAME_DMA_MEM "tas_memory"
/** Name for flexnic internal shared memory region. */
#define FLEXNIC_NAME_INTERNAL_MEM "tas_internal"

/** Size of the info shared memory region. */
#define FLEXNIC_INFO_BYTES 0x1000

/** Indicates that flexnic is done initializing. */
#define FLEXNIC_FLAG_READY 1
/** Indicates that huge pages should be used for the internal and dma memory */
#define FLEXNIC_FLAG_HUGEPAGES 2

/** Info struct: layout of info shared memory region */
struct flexnic_info {
  /** Flags: see FLEXNIC_FLAG_* */
  uint64_t flags;
  /** Size of flexnic dma memory in bytes. */
  uint64_t dma_mem_size;
  /** Size of internal flexnic memory in bytes. */
  uint64_t internal_mem_size;
  /** export mac address */
  uint64_t mac_address;
  /** Cycles to poll before blocking for application */
  uint64_t poll_cycle_app;
  /** Cycles to poll before blocking for TAS */
  uint64_t poll_cycle_tas;
  /** Number of queues in queue manager */
  uint32_t qmq_num;
  /** Number of cores in flexnic emulator */
  uint32_t cores_num;
} __attribute__((packed));



/******************************************************************************/
/* Kernel RX queue */

#define FLEXTCP_PL_KRX_INVALID 0x0
#define FLEXTCP_PL_KRX_PACKET 0x1

/** Kernel RX queue entry */
struct flextcp_pl_krx {
  uint64_t addr;
  union {
    struct {
      uint16_t len;
      uint16_t fn_core;
      uint16_t flow_group;
    } packet;
    uint8_t raw[55];
  } __attribute__((packed)) msg;
  volatile uint8_t type;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct flextcp_pl_krx) == 64, krx_size);


/******************************************************************************/
/* Kernel TX queue */

#define FLEXTCP_PL_KTX_INVALID 0x0
#define FLEXTCP_PL_KTX_PACKET 0x1
#define FLEXTCP_PL_KTX_CONNRETRAN 0x2
#define FLEXTCP_PL_KTX_PACKET_NOTS 0x3

/** Kernel TX queue entry */
struct flextcp_pl_ktx {
  union {
    struct {
      uint64_t addr;
      uint16_t len;
    } packet;
    struct {
      uint32_t flow_id;
    } connretran;
    uint8_t raw[63];
  } __attribute__((packed)) msg;
  volatile uint8_t type;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct flextcp_pl_ktx) == 64, ktx_size);


/******************************************************************************/
/* App RX queue */

#define FLEXTCP_PL_ARX_INVALID    0x0
#define FLEXTCP_PL_ARX_CONNUPDATE 0x1

#define FLEXTCP_PL_ARX_FLRXDONE  0x1

/** Update receive and transmit buffer of flow */
struct flextcp_pl_arx_connupdate {
  uint64_t opaque;
  uint32_t rx_bump;
  uint32_t rx_pos;
  uint32_t tx_bump;
  uint8_t flags;
} __attribute__((packed));

/** Application RX queue entry */
struct flextcp_pl_arx {
  union {
    struct flextcp_pl_arx_connupdate connupdate;
    uint8_t raw[31];
  } __attribute__((packed)) msg;
  volatile uint8_t type;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct flextcp_pl_arx) == 32, arx_size);

/******************************************************************************/
/* App TX queue */

#define FLEXTCP_PL_ATX_CONNUPDATE 0x1

#define FLEXTCP_PL_ATX_FLTXDONE  0x1

/** Application TX queue entry */
struct flextcp_pl_atx {
  union {
    struct {
      uint32_t rx_bump;
      uint32_t tx_bump;
      uint32_t flow_id;
      uint16_t bump_seq;
      uint8_t  flags;
    } __attribute__((packed)) connupdate;
    uint8_t raw[15];
  } __attribute__((packed)) msg;
  volatile uint8_t type;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct flextcp_pl_atx) == 16, atx_size);

/******************************************************************************/
/* Internal flexnic memory */

#define FLEXNIC_PL_APPST_NUM        8
#define FLEXNIC_PL_APPST_CTX_NUM   31
#define FLEXNIC_PL_APPST_CTX_MCS   16
#define FLEXNIC_PL_APPCTX_NUM      16
#define FLEXNIC_PL_FLOWST_NUM     (128 * 1024)
#define FLEXNIC_PL_FLOWHT_ENTRIES (FLEXNIC_PL_FLOWST_NUM * 2)
#define FLEXNIC_PL_FLOWHT_NBSZ      4

/** Application state */
struct flextcp_pl_appst {
  /********************************************************/
  /* read-only fields */

  /** Number of contexts */
  uint16_t ctx_num;

  /** IDs of contexts */
  uint16_t ctx_ids[FLEXNIC_PL_APPST_CTX_NUM];
} __attribute__((packed));


/** Application context registers */
struct flextcp_pl_appctx {
  /********************************************************/
  /* read-only fields */
  uint64_t rx_base;
  uint64_t tx_base;
  uint32_t rx_len;
  uint32_t tx_len;
  uint32_t appst_id;
  int	   evfd;

  /********************************************************/
  /* read-write fields */
  uint64_t last_ts;
  uint32_t rx_head;
  uint32_t tx_head;
  uint32_t rx_avail;
} __attribute__((packed));

/** Enable out of order receive processing members */
#define FLEXNIC_PL_OOO_RECV 1

#define FLEXNIC_PL_FLOWST_SLOWPATH 1
#define FLEXNIC_PL_FLOWST_ECN 8
#define FLEXNIC_PL_FLOWST_TXFIN 16
#define FLEXNIC_PL_FLOWST_RXFIN 32
#define FLEXNIC_PL_FLOWST_RX_MASK (~63ULL)

/** Flow state registers */
struct flextcp_pl_flowst {
  /********************************************************/
  /* read-only fields */

  /** Opaque flow identifier from application */
  uint64_t opaque;

  /** Base address of receive buffer */
  uint64_t rx_base_sp;
  /** Base address of transmit buffer */
  uint64_t tx_base;

  /** Length of receive buffer */
  uint32_t rx_len;
  /** Length of transmit buffer */
  uint32_t tx_len;

  beui32_t local_ip;
  beui32_t remote_ip;

  beui16_t local_port;
  beui16_t remote_port;

  /** Remote MAC address */
  struct eth_addr remote_mac;

  /** Doorbell ID (identifying the app ctx to use) */
  uint16_t db_id;

  /** Flow group for this connection (rss bucket) */
  uint16_t flow_group;
  /** Sequence number of queue pointer bumps */
  uint16_t bump_seq;

  /** TX Window scale factor */
  uint8_t tx_window_scale;
  /** RX Window scale factor */
  uint8_t rx_window_scale;
  // 58

  /********************************************************/
  /* read-write fields */

  /** Duplicate ack count */
  uint16_t rx_dupack_cnt;

  /** Bytes available for received segments at next position */
  uint32_t rx_avail;
  // 64
  /** Offset in buffer to place next segment */
  uint32_t rx_next_pos;
  /** Next sequence number expected */
  uint32_t rx_next_seq;
  /** Bytes available in remote end for received segments */
  uint32_t rx_remote_avail;

  /** spin lock */
  volatile uint32_t lock;

#ifdef FLEXNIC_PL_OOO_RECV
  /* Start of interval of out-of-order received data */
  uint32_t rx_ooo_start;
  /* Length of interval of out-of-order received data */
  uint32_t rx_ooo_len;
#endif

  /** Number of bytes available to be sent */
  uint32_t tx_avail;
  /** Number of bytes up to next pos in the buffer that were sent but not
   * acknowledged yet. */
  uint32_t tx_sent;
  /** Offset in buffer for next segment to be sent */
  uint32_t tx_next_pos;
  /** Sequence number of next segment to be sent */
  uint32_t tx_next_seq;
  /** Timestamp to echo in next packet */
  uint32_t tx_next_ts;

  /** Congestion control rate [kbps] */
  uint32_t tx_rate;
  /** Counter drops */
  uint16_t cnt_tx_drops;
  /** Counter acks */
  uint16_t cnt_rx_acks;
  /** Counter bytes sent */
  uint32_t cnt_rx_ack_bytes;
  /** Counter acks marked */
  uint32_t cnt_rx_ecn_bytes;
  /** RTT estimate */
  uint32_t rtt_est;

// 128
} __attribute__((packed, aligned(64)));

#define FLEXNIC_PL_FLOWHTE_VALID  (1 << 31)
#define FLEXNIC_PL_FLOWHTE_POSSHIFT 29

/** Flow lookup table entry */
struct flextcp_pl_flowhte {
  uint32_t flow_id;
  uint32_t flow_hash;
} __attribute__((packed));


#define FLEXNIC_PL_MAX_FLOWGROUPS 4096

/** Layout of internal pipeline memory */
struct flextcp_pl_mem {
  /* registers for application context queues */
  struct flextcp_pl_appctx appctx[FLEXNIC_PL_APPST_CTX_MCS][FLEXNIC_PL_APPCTX_NUM];

  /* registers for flow state */
  struct flextcp_pl_flowst flowst[FLEXNIC_PL_FLOWST_NUM];

  /* flow lookup table */
  struct flextcp_pl_flowhte flowht[FLEXNIC_PL_FLOWHT_ENTRIES];

  /* registers for kernel queues */
  struct flextcp_pl_appctx kctx[FLEXNIC_PL_APPST_CTX_MCS];

  /* registers for application state */
  struct flextcp_pl_appst appst[FLEXNIC_PL_APPST_NUM];

  uint8_t flow_group_steering[FLEXNIC_PL_MAX_FLOWGROUPS];
} __attribute__((packed));

/** @} */

#endif /* ndef FLEXTCP_PLIF_H_ */
