#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "shmring.h"

struct ring_buffer* shmring_init(void *base_addr, size_t size)
{
    struct ring_buffer *ring;
    struct ring_header *hdr;

    ring = (struct ring_buffer *) malloc(sizeof(struct ring_buffer));
    if (ring == NULL)
    {
      fprintf(stderr, "shmring_init: failed to allocate ring buffer.\n");
      return NULL;
    }

    ring->hdr_addr = base_addr;
    ring->buf_addr = base_addr + sizeof(struct ring_header);
    ring->size = size;

    hdr = (struct ring_header *) hdr;
    hdr->read_pos = 0;
    hdr->write_pos = 0;
    hdr->full = 0;
    hdr->ring_size = size - sizeof(struct ring_header);

  return ring;
}

int shmring_pop(struct ring_buffer *rx_ring, void *dst, size_t size)
{
  struct ring_header *hdr;

  hdr = (struct ring_header *) rx_ring->hdr_addr;

  /* Ring is empty */
  if (!hdr->full && (hdr->write_pos == hdr->read_pos))
  {
    return -1;
  }

  

  if (hdr->write_pos != hdr->read_pos)
  {
    hdr->full = 0;
  }

  return 0;
}

int shmring_push(struct ring_buffer *tx_ring, void *src, size_t size)
{
  struct ring_header *hdr;

  hdr = (struct ring_header *) tx_ring->hdr_addr;

  /* Return error if ring is full */
  if (hdr->full)
  {
    return -1;
  }

  memcpy(tx_ring->buf_addr + hdr->write_pos, src, size);
  hdr->write_pos += size;
  hdr->write_pos %= hdr->ring_size;

  if (hdr->write_pos == hdr->read_pos)
  {
    hdr->full = 1;
  }

  return 0;
}