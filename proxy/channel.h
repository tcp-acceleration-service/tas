#ifndef CHANNEL_H_
#define CHANNEL_H_

#include "shmring.h"

#define CHAN_OFFSET 0x1000
#define CHAN_SIZE 0x1000

struct channel {
  struct ring_buffer *rx;
  struct ring_buffer *tx;
};

struct channel * channel_init(void* tx_addr, void* rx_addr, uint64_t size);
int channel_write(struct channel *chan, void *buf, size_t size);
int channel_read(struct channel *chan, void *buf, size_t size);

#endif /* ndef CHANNEL_H_ */