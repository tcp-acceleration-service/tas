#ifndef SHMRING_H_
#define SHMRING_H_

#include <stddef.h>

struct ring_header {
  int write_pos;
  int read_pos;
  int full;
  size_t ring_size;
};

struct ring_buffer {
  struct ring_header hdr;
  void *buf_addr;
};

struct ring_buffer* shmring_init(void *base_addr, size_t size);
void shmring_reset(struct ring_buffer *ring, size_t size);
size_t shmring_pop(struct ring_buffer *rx_ring, void *buf, size_t size);
size_t shmring_read(struct ring_buffer *rx_ring, void *buf, size_t size);
size_t shmring_push(struct ring_buffer *tx_ring, void *buf, size_t size);

#endif /* ndef SHMRING_H_ */
