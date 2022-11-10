#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "shmring.h"

size_t shmring_get_freesz(struct ring_buffer *ring);
size_t shmring_pop_fragmented(struct ring_buffer *rx_ring, 
    void *dst, size_t size);
size_t shmring_read_fragmented(struct ring_buffer *rx_ring, 
    void *dst, size_t size);
size_t shmring_push_fragmented(struct ring_buffer *tx_ring, 
    void *src, size_t size);

struct ring_buffer* shmring_init(void *base_addr, size_t size)
{
    struct ring_buffer *ring;

    ring = (struct ring_buffer *) malloc(sizeof(struct ring_buffer));
    if (ring == NULL)
    {
      fprintf(stderr, "shmring_init: failed to allocate ring buffer.\n");
      return NULL;
    }

    ring->hdr_addr = base_addr;
    ring->buf_addr = base_addr + sizeof(struct ring_header);
    ring->size = size;

    return ring;
}

/* Resets read and write pos to zero and sets ring size to full */
void shmring_reset(struct ring_buffer *ring, size_t size)
{
    struct ring_header *hdr = (struct ring_header *) ring->hdr_addr;

    hdr->read_pos = 0;
    hdr->write_pos = 0;
    hdr->full = 0;
    hdr->ring_size = size - sizeof(struct ring_header);
}

/* Reads from ring and updates read pos */
size_t shmring_pop(struct ring_buffer *rx_ring, void *dst, size_t size)
{
  size_t ret, freesz;
  struct ring_header *hdr;

  hdr = (struct ring_header *) rx_ring->hdr_addr;

  /* Return error if there is not enough written bytes
     to read in the ring */
  freesz = shmring_get_freesz(rx_ring);
  printf("shmring_pop: ring_size=%ld.\n", hdr->ring_size);
  printf("shmring_pop: freesz=%ld.\n", freesz);
  printf("shmring_pop: size=%ld.\n", size);
  printf("shmring_pop: ring_size - freesz = %ld.\n", hdr->ring_size - freesz);
  printf("shmring_pop: bool=%d.\n", (hdr->ring_size - freesz) < size);
  if ((hdr->ring_size - freesz) < size)
  {
    fprintf(stderr, "shmring_pop: not enough written bytes in ring.\n");
    return 0;
  }

  /* If data loops over do a fragmented read to
     the end and from the beginning of the ring*/
  if ((hdr->ring_size - hdr->read_pos) < size)
  {
    ret = shmring_pop_fragmented(rx_ring, dst, size);
    return ret;
  }

  if (memcpy(dst, rx_ring->buf_addr + hdr->read_pos, size) 
      == NULL)
  {
    fprintf(stderr, "shmring_pop: memcpy failed.\n");
    return 0;
  }

  hdr->read_pos += size;
  hdr->read_pos %= hdr->ring_size;

  /* Mark the ring as not full */
  if (hdr->write_pos != hdr->read_pos)
  {
    hdr->full = 0;
  }

  return size;
}

/* Reads from regions in beginning and end of ring and updates
   read pos */
size_t shmring_pop_fragmented(struct ring_buffer *rx_ring, 
    void *dst, size_t size)
{
  size_t sz1, sz2;
  void *ret;
  struct ring_header *hdr = rx_ring->hdr_addr;
  
  /* Do first read on the ring */
  sz1 = hdr->ring_size - hdr->read_pos;
  ret = memcpy(dst, rx_ring->buf_addr + hdr->read_pos, sz1);
  if (ret == NULL)
  {
    fprintf(stderr, "shmring_pop_fragmented: first read failed.\n");
    return 0;
  }

  /* Do second read on the ring */
  sz2 = size - sz1;
  ret = memcpy(dst + sz1, rx_ring->buf_addr, sz2);
  if (ret == NULL)
  {
    fprintf(stderr, "shmring_pop_fragmented: second read failed.\n");
    return 0;
  }

  hdr->read_pos += size;
  hdr->read_pos %= hdr->ring_size;

  /* Mark the ring as not full */
  if (hdr->write_pos != hdr->read_pos)
  {
    hdr->full = 0;
  }

  return size;
}

/* Reads from ring buffer but does not move read pos */
size_t shmring_read(struct ring_buffer *rx_ring, void *dst, size_t size)
{
  size_t ret, freesz;
  struct ring_header *hdr;

  hdr = (struct ring_header *) rx_ring->hdr_addr;

  /* Return error if there is not enough written bytes
     to read in the ring */
  freesz = shmring_get_freesz(rx_ring);
  if ((hdr->ring_size - freesz) < size)
  {
    fprintf(stderr, "shmring_read: not enough written bytes in ring.\n");
    return 0;
  }

  /* If data loops over do a fragmented read to
     the end and from the beginning of the ring*/
  if ((hdr->ring_size - hdr->read_pos) < size)
  {
    ret = shmring_read_fragmented(rx_ring, dst, size);
    return ret;
  }

  if (memcpy(dst, rx_ring->buf_addr + hdr->read_pos, size) 
      == NULL)
  {
    fprintf(stderr, "shmring_read: memcpy failed.\n");
    return 0;
  }

  return size;
}

/* Reads from fragmented ring buffer region but does not move
   read pos */
size_t shmring_read_fragmented(struct ring_buffer *rx_ring, 
    void *dst, size_t size)
{
  int sz1, sz2;
  void *ret;
  struct ring_header *hdr = rx_ring->hdr_addr;
  
  /* Do first read on the ring */
  sz1 = hdr->ring_size - hdr->read_pos;
  ret = memcpy(dst, rx_ring->buf_addr + hdr->read_pos, sz1);
  if (ret == NULL)
  {
    fprintf(stderr, "shmring_pop_fragmented: first read failed.\n");
    return 0;
  }

  /* Do second read on the ring */
  sz2 = size - sz1;
  ret = memcpy(dst + sz1, rx_ring->buf_addr, sz2);
  if (ret == NULL)
  {
    fprintf(stderr, "shmring_pop_fragmented: second read failed.\n");
    return 0;
  }

  return size;
}

size_t shmring_push(struct ring_buffer *tx_ring, void *src, size_t size)
{
  size_t ret, freesz;
  struct ring_header *hdr;

  hdr = (struct ring_header *) tx_ring->hdr_addr;

  /* Return error if there is not enough space in ring */
  freesz = shmring_get_freesz(tx_ring);
  if (freesz < size)
  {
    return 0;
  }

  /* If data loops over do a fragmented write
     to the end and beginning of the ring */
  if ((hdr->ring_size - hdr->write_pos) < size)
  {
    ret = shmring_push_fragmented(tx_ring, src, size);
    return ret;
  }

  if (memcpy(tx_ring->buf_addr + hdr->write_pos, src, size) 
      == NULL)
  {
    fprintf(stderr, "shmring_push: memcpy failed.\n");
    return 0;
  }

  hdr->write_pos += size;
  hdr->write_pos %= hdr->ring_size;

  /* Mark the ring as full */
  if (hdr->write_pos == hdr->read_pos)
  {
    hdr->full = 1;
  }

  return size;
}

size_t shmring_push_fragmented(struct ring_buffer *tx_ring, 
    void *src, size_t size)
{
  int sz1, sz2;
  void *ret;
  struct ring_header *hdr = tx_ring->hdr_addr;

  /* Do first write to the end of the ring */
  sz1 = hdr->ring_size - hdr->write_pos;
  ret = memcpy(tx_ring->buf_addr + hdr->write_pos, src, sz1);
  if (ret == NULL)
  {
    fprintf(stderr, "shmring_push_fragmented: first write failed.\n");
    return 0;
  }

  /* Do the second write from the beginning of the ring */
  sz2 = size - sz1;
  ret = memcpy(tx_ring->buf_addr, src + sz1, sz2);
  if (ret == NULL)
  {
    fprintf(stderr, "shmring_push_fragmented: second write failed.\n");
    return 0;
  }

  hdr->write_pos += size;
  hdr->write_pos %= hdr->ring_size;

  if (hdr->write_pos == hdr->read_pos)
  {
    hdr->full = 1;
  }

  return size;
}

size_t shmring_get_freesz(struct ring_buffer *ring)
{
  struct ring_header* hdr = ring->hdr_addr;
  int w_pos = hdr->write_pos;
  int r_pos = hdr->read_pos;

  if (!hdr->full && (w_pos == r_pos))
    return hdr->ring_size;

  if (w_pos > r_pos)
    return (hdr->ring_size - w_pos) + r_pos;
  else
    return r_pos - w_pos;
}
