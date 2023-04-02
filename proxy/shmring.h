#ifndef SHMRING_H_
#define SHMRING_H_

#include <stddef.h>
#include <pthread.h>

struct ring_header {
  volatile int write_pos;
  volatile int read_pos;
  volatile int full;
  volatile size_t ring_size;
  pthread_mutex_t mux;
} __attribute__((packed));

struct ring_buffer {
  struct ring_header *hdr_addr;
  void *buf_addr;
  size_t size;
};

struct ring_buffer* shmring_init(void *base_addr, size_t size);
void shmring_reset(struct ring_buffer *ring, size_t size);
int shmring_is_empty(struct ring_buffer *ring);
int shmring_init_mux(struct ring_buffer *ring);
int shmring_lock(struct ring_buffer *ring);
int shmring_unlock(struct ring_buffer *ring);
size_t shmring_pop(struct ring_buffer *rx_ring, void *buf, size_t size);
size_t shmring_read(struct ring_buffer *rx_ring, void *buf, size_t size);
size_t shmring_push(struct ring_buffer *tx_ring, void *buf, size_t size);
size_t shmring_get_freesz(struct ring_buffer *ring);

#endif /* ndef SHMRING_H_ */
