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

    ring->buf_addr = base_addr;

    return ring;
}

/* Resets read and write pos to zero and sets ring size to full */
void shmring_reset(struct ring_buffer *ring, size_t size)
{
    ring->hdr.read_pos = 0;
    ring->hdr.write_pos = 0;
    ring->hdr.full = 0;
    ring->hdr.ring_size = size;
}

/* Reads from ring and updates read pos */
size_t shmring_pop(struct ring_buffer *rx_ring, void *dst, size_t size)
{
  size_t ret, freesz;
  struct ring_header *hdr;

  hdr = &rx_ring->hdr;

  /* Return error if there is not enough written bytes
     to read in the ring */
  freesz = shmring_get_freesz(rx_ring);
  if ((hdr->ring_size - freesz) < size)
  {
    fprintf(stderr, "shmring_pop: not enough written bytes in ring.\n");
    return 0;
  }

  printf("before pop tx buf_addr = %p, read_pos = %d, size = %ld, dst = %p, ring_size = %ld\n", 
      rx_ring->buf_addr, hdr->read_pos, size, dst, hdr->ring_size);

  /* If data loops over do a fragmented read to
     the end and from the beginning of the ring*/
  if ((hdr->ring_size - hdr->read_pos) < size)
  {
    ret = shmring_pop_fragmented(rx_ring, dst, size);
    printf("after pop tx buf_addr = %p, read_pos = %d, size = %ld, dst = %p, ring_size = %ld\n", 
      rx_ring->buf_addr, hdr->read_pos, size, dst, hdr->ring_size);
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

  printf("after pop tx buf_addr = %p, read_pos = %d, size = %ld, dst = %p, ring_size = %ld\n", 
      rx_ring->buf_addr, hdr->read_pos, size, dst, hdr->ring_size);

  return size;
}

/* Reads from regions in beginning and end of ring and updates
   read pos */
size_t shmring_pop_fragmented(struct ring_buffer *rx_ring, 
    void *dst, size_t size)
{
  size_t sz1, sz2;
  void *ret;
  struct ring_header *hdr = &rx_ring->hdr;
  
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

  hdr = &rx_ring->hdr;

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
  struct ring_header *hdr = &rx_ring->hdr;
  
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
  printf("shmring_push\n");
  size_t ret, freesz;
  struct ring_header *hdr;

  hdr = &tx_ring->hdr;

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
    printf("fragmented write\n");
    ret = shmring_push_fragmented(tx_ring, src, size);
    printf("after push tx buf_addr = %p, write_pos = %d, size = %ld, src = %p, ring_size = %ld, ring_size = %p\n", 
      tx_ring->buf_addr, hdr->write_pos, size, src, hdr->ring_size, &hdr->ring_size);
    return ret;
  }

  printf("before push tx buf_addr = %p, write_pos = %d, size = %ld, src = %p, ring_size = %ld, ring_size_p = %p\n", 
      tx_ring->buf_addr, hdr->write_pos, size, src, hdr->ring_size, &hdr->ring_size);
  printf("tx_ring->buf_addr + write_pos = %c\n", *((char *) tx_ring->buf_addr + hdr->write_pos));
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

  printf("after push tx buf_addr = %p, write_pos = %d, size = %ld, src = %p, ring_size = %ld, ring_size_p = %p\n", 
      tx_ring->buf_addr, hdr->write_pos, size, src, hdr->ring_size, &hdr->ring_size);

  return size;
}

size_t shmring_push_fragmented(struct ring_buffer *tx_ring, 
    void *src, size_t size)
{
  int sz1, sz2;
  void *ret;
  struct ring_header *hdr = &tx_ring->hdr;

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
  struct ring_header* hdr = &ring->hdr;
  int w_pos = hdr->write_pos;
  int r_pos = hdr->read_pos;

  if (!hdr->full && (w_pos == r_pos))
    return hdr->ring_size;

  if (w_pos > r_pos)
    return (hdr->ring_size - w_pos) + r_pos;
  else
    return r_pos - w_pos;
}
