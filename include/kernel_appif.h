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

#ifndef KERNEL_APPIF_H_
#define KERNEL_APPIF_H_

#include <stdint.h>

#include <utils.h>

/******************************************************************************/
/* Unix socket for initialization with application */

#define KERNEL_SOCKET_PATH "\0flexnic_os"
#define KERNEL_UXSOCK_MAXQ 8

struct kernel_uxsock_request {
  uint32_t rxq_len;
  uint32_t txq_len;
} __attribute__((packed));

struct kernel_uxsock_response {
  uint64_t app_out_off;
  uint64_t app_in_off;

  uint32_t app_out_len;
  uint32_t app_in_len;

  uint32_t status;
  uint16_t flexnic_db_id;
  uint16_t flexnic_qs_num;

  struct {
    uint64_t rxq_off;
    uint64_t txq_off;
  } __attribute__((packed)) flexnic_qs[];
} __attribute__((packed));

/******************************************************************************/
/* App -> Kernel */

enum kernel_appout_type {
  KERNEL_APPOUT_INVALID = 0,
  KERNEL_APPOUT_CONN_OPEN,
  KERNEL_APPOUT_CONN_CLOSE,
  KERNEL_APPOUT_CONN_MOVE,
  KERNEL_APPOUT_LISTEN_OPEN,
  KERNEL_APPOUT_LISTEN_CLOSE,
  KERNEL_APPOUT_ACCEPT_CONN,
  KERNEL_APPOUT_REQ_SCALE,
};

/** Open a new connection */
struct kernel_appout_conn_open {
  uint64_t opaque;
  uint32_t remote_ip;
  uint32_t flags;
  uint16_t remote_port;
} __attribute__((packed));

#define KERNEL_APPOUT_CLOSE_RESET 0x1
/** Close connection */
struct kernel_appout_conn_close {
  uint64_t opaque;
  uint32_t remote_ip;
  uint32_t local_ip;
  uint16_t remote_port;
  uint16_t local_port;
  uint32_t flags;
} __attribute__((packed));

/** Move connection to new context */
struct kernel_appout_conn_move {
  uint64_t opaque;
  uint32_t remote_ip;
  uint32_t local_ip;
  uint16_t remote_port;
  uint16_t local_port;
  uint16_t db_id;
} __attribute__((packed));

#define KERNEL_APPOUT_LISTEN_REUSEPORT 0x1
/** Open listener */
struct kernel_appout_listen_open {
  uint64_t opaque;
  uint32_t backlog;
  uint16_t local_port;
  uint8_t  flags;
} __attribute__((packed));

/** Close listener */
struct kernel_appout_listen_close {
  uint64_t opaque;
  uint16_t local_port;
} __attribute__((packed));

/** Accept connection on listening socket */
struct kernel_appout_accept_conn {
  uint64_t listen_opaque;
  uint64_t conn_opaque;
  uint16_t local_port;
} __attribute__((packed));

/** Request scale to specified number of cores */
struct kernel_appout_req_scale {
  uint32_t num_cores;
} __attribute__((packed));

/** Common struct for events on kernel -> app queue */
struct kernel_appout {
  union {
    struct kernel_appout_conn_open    conn_open;
    struct kernel_appout_conn_close   conn_close;
    struct kernel_appout_conn_move    conn_move;

    struct kernel_appout_listen_open  listen_open;
    struct kernel_appout_listen_close listen_close;
    struct kernel_appout_accept_conn  accept_conn;

    struct kernel_appout_req_scale    req_scale;

    uint8_t raw[63];
  } __attribute__((packed)) data;
  uint8_t type;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct kernel_appout) == 64, kernel_appout_size);


/******************************************************************************/
/* Kernel -> App */

enum kernel_appin_type {
  KERNEL_APPIN_INVALID = 0,
  KERNEL_APPIN_STATUS_CONN_CLOSE,
  KERNEL_APPIN_STATUS_CONN_MOVE,
  KERNEL_APPIN_STATUS_LISTEN_OPEN,
  KERNEL_APPIN_STATUS_LISTEN_CLOSE,
  KERNEL_APPIN_STATUS_REQ_SCALE,
  KERNEL_APPIN_CONN_OPENED,
  KERNEL_APPIN_LISTEN_NEWCONN,
  KERNEL_APPIN_ACCEPTED_CONN,
};

/** Generic operation status */
struct kernel_appin_status {
  uint64_t opaque;
  int32_t  status;
} __attribute__((packed));

/** New connection opened */
struct kernel_appin_conn_opened {
  uint64_t opaque;
  uint64_t rx_off;
  uint64_t tx_off;
  uint32_t rx_len;
  uint32_t tx_len;
  int32_t  status;
  uint32_t seq_rx;
  uint32_t seq_tx;
  uint32_t flow_id;
  uint32_t local_ip;
  uint16_t local_port;
  uint16_t fn_core;
} __attribute__((packed));

/** New connection on listener received */
struct kernel_appin_listen_newconn {
  uint64_t opaque;
  uint32_t remote_ip;
  uint16_t remote_port;
} __attribute__((packed));

/** Accepted connection on listener */
struct kernel_appin_accept_conn {
  uint64_t opaque;
  uint64_t rx_off;
  uint64_t tx_off;
  uint32_t rx_len;
  uint32_t tx_len;
  int32_t  status;
  uint32_t seq_rx;
  uint32_t seq_tx;
  uint32_t flow_id;
  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t remote_port;
  uint16_t fn_core;
} __attribute__((packed));

/** Common struct for events on app -> kernel queue */
struct kernel_appin {
  union {
    struct kernel_appin_status          status;
    struct kernel_appin_conn_opened     conn_opened;
    struct kernel_appin_listen_newconn  listen_newconn;
    struct kernel_appin_accept_conn     accept_connection;
    uint8_t raw[63];
  } __attribute__((packed)) data;
  uint8_t type;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct kernel_appin) == 64, kernel_appin_size);

#endif /* ndef KERNEL_APPIF_H_ */
