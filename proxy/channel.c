
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "channel.h"
#include "shmring.h"

struct channel * channel_init(void* tx_addr, void* rx_addr, uint64_t size)
{
  struct channel *chan;
  struct ring_buffer *tx_buf, *rx_buf;

  chan = (struct channel *) malloc(sizeof(struct channel));
  if (chan == NULL)
  {
    fprintf(stderr, "channel_init: failed to allocate memory for channel.\n");
    goto free_chan;
  }

  /* Only init the tx channel, since the other side will
     already have initialized the rx channel */
  tx_buf = shmring_init(tx_addr, CHAN_SIZE);
  if (tx_buf == NULL)
  {
    fprintf(stderr, "channel_init: failed to allocate tx buf.\n");
    goto free_chan;
  }

  rx_buf = shmring_init(rx_addr, CHAN_SIZE);
  if (rx_buf == NULL)
  {
    fprintf(stderr, "channel_init: failed to malloc rx_buf.\n");
    goto free_tx_buf;
  }

  /* Only reset tx_ring since other side will have already
     reset the rx_ring */
  shmring_reset(tx_buf, CHAN_SIZE);

  chan->tx = tx_buf;
  chan->rx = rx_buf; 
  printf("channel_init: chan->tx=%p.\n", chan->tx);
  printf("channel_init: chan->rx=%p.\n", chan->rx);
  return chan;

free_tx_buf:
  free(tx_buf);
free_chan:
  free(chan);

  return NULL;

}

int channel_write(struct channel *chan, void *buf, size_t size)
{
  int ret;
  ret = shmring_push(chan->tx, buf, size);

  if (ret < 0)
  {
    fprintf(stderr, "channel_write: failed to write to shm ring.\n");
    return -1;
  }
  
  return ret;
}

int channel_read(struct channel *chan, void *buf, size_t size)
{
  int ret;
  ret = shmring_pop(chan->tx, buf, size);

  if (ret < 0)
  {
    fprintf(stderr, "channel_read: failed to read from shm ring.\n");
    return -1;
  }
  
  return ret;
}
