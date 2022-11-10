#ifndef CHANNEL_H_
#define CHANNEL_H_

#include "shmring.h"
#include "../include/tas_memif.h"

#define CHAN_OFFSET 0x1000
#define CHAN_SIZE 0x1000

#define MSG_TYPE_HELLO 1
#define MSG_TYPE_TASINFO_REQ 2
#define MSG_TYPE_TASINFO_RES 3
#define MSG_TYPE_CONTEXT_REQ 4
#define MSG_TYPE_CONTEXT_RES 5

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
} __attribute__((packed));

struct context_res_msg {
  uint8_t msg_type;
} __attribute__((packed));

struct channel {
  struct ring_buffer *rx;
  struct ring_buffer *tx;
} __attribute__((packed));

struct channel * channel_init(void* tx_addr, void* rx_addr, uint64_t size);
int channel_write(struct channel *chan, void *buf, size_t size);
int channel_read(struct channel *chan, void *buf, size_t size);
uint8_t channel_get_msg_type(struct channel *chan);
size_t channel_get_type_size(uint8_t type);

#endif /* ndef CHANNEL_H_ */