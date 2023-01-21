#ifndef CHANNEL_H_
#define CHANNEL_H_

#include "shmring.h"
#include <stdint.h>

#include <tas_ll.h>
#include "proxy.h"
#include "../include/tas_memif.h"

#define CHAN_OFFSET 0x0000
#define CHAN_SIZE 0x2000

#define MSG_TYPE_HELLO 1
#define MSG_TYPE_TASINFO_REQ 2
#define MSG_TYPE_TASINFO_RES 3
#define MSG_TYPE_CONTEXT_REQ 4
#define MSG_TYPE_CONTEXT_RES 5
#define MSG_TYPE_VPOKE 6
#define MSG_TYPE_NEWAPP_REQ 7
#define MSG_TYPE_NEWAPP_RES 8

/* Used to get CTX_RESP_MAX_SIZE... kinda ugly */
struct kernel_uxsock_response *placeholder_resp;

#define PLACEHOLDER_SIZE sizeof(placeholder_resp->flexnic_qs[0])
#define CTX_RESP_MAX_SIZE sizeof(struct kernel_uxsock_response) + FLEXTCP_MAX_FTCPCORES * PLACEHOLDER_SIZE

struct hello_msg {
  uint8_t msg_type;
  int n_cores;
} __attribute__((packed));

struct tasinfo_req_msg {
  uint8_t msg_type;
} __attribute__((packed));

struct tasinfo_res_msg {
  uint8_t msg_type;
  char flexnic_info[FLEXNIC_INFO_BYTES];
} __attribute__((packed));

struct context_req_msg {
  uint8_t msg_type;
  uint32_t actx_evfd;
  uint32_t ctxreq_id;
  uint32_t app_id;
  struct proxy_context_req context_req;
} __attribute__((packed));

struct context_res_msg {
  uint8_t msg_type;
  uint32_t ctxreq_id;
  uint32_t app_id;
  ssize_t resp_size;
  uint8_t resp[CTX_RESP_MAX_SIZE];
} __attribute__((packed));

struct vpoke_msg {
  uint8_t msg_type;
  uint32_t ctxreq_id;
} __attribute__((packed));

struct newapp_req_msg {
  uint8_t msg_type;
  int cfd;
} __attribute__((packed));

struct newapp_res_msg {
  uint8_t msg_type;
  int cfd;
} __attribute__((packed));

struct channel {
  struct ring_buffer *rx;
  struct ring_buffer *tx;
} __attribute__((packed));

struct channel * channel_init(void* tx_addr, void* rx_addr, uint64_t size);
size_t channel_write(struct channel *chan, void *buf, size_t size);
size_t channel_read(struct channel *chan, void *buf, size_t size);
uint8_t channel_get_msg_type(struct channel *chan);
size_t channel_get_type_size(uint8_t type);

#endif /* ndef CHANNEL_H_ */
