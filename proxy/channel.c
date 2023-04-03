
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

  chan->tx = tx_buf;
  chan->rx = rx_buf; 
  return chan;

free_tx_buf:
  free(tx_buf);
free_chan:
  free(chan);

  return NULL;

}

size_t channel_write(struct channel *chan, void *buf, size_t size)
{
  size_t ret, free_sz;

  do {
    shmring_lock(chan->tx);
    free_sz = shmring_get_freesz(chan->tx);
    if (free_sz >= size) 
    {
      ret = shmring_push(chan->tx, buf, size);
      shmring_unlock(chan->tx);
      break;
    }
    shmring_unlock(chan->tx);
  } while(free_sz < size);

  if (ret == 0)
  {
    fprintf(stderr, "channel_write: failed to write to shm ring.\n");
    return 0;
  }
  
  return ret;
}

size_t channel_read(struct channel *chan, void *buf, size_t size)
{
  size_t ret;

  shmring_lock(chan->rx);
  ret = shmring_pop(chan->rx, buf, size);
  shmring_unlock(chan->rx);

  if (ret == 0)
  {
    fprintf(stderr, "channel_read: failed to read from shm ring.\n");
    return 0;
  }
  
  return ret;
}

/* Gets the type of the next message to read */
uint8_t channel_get_msg_type(struct channel *chan)
{
  
  int ret;
  uint8_t type;

  /* First byte is always message type */
  shmring_lock(chan->rx);
  ret = shmring_read(chan->rx, &type, sizeof(uint8_t));
  shmring_unlock(chan->rx);

  if (ret < sizeof(uint8_t))
  {
    fprintf(stderr, "channel_get_msg_type: failed to get msg type.\n");
    return -1;
  }

  return type;
}

size_t channel_get_type_size(uint8_t type)
{
  switch(type)
  {
    case MSG_TYPE_HELLO:
      return sizeof(struct hello_msg);
      break;
    case MSG_TYPE_TASINFO_REQ:
      return sizeof(struct tasinfo_req_msg);
      break;
    case MSG_TYPE_TASINFO_RES:
      return sizeof(struct tasinfo_res_msg);
      break;
    case MSG_TYPE_CONTEXT_REQ:
      return sizeof(struct context_req_msg);
      break;
    case MSG_TYPE_CONTEXT_RES:
      return sizeof(struct context_res_msg);
      break;
    case MSG_TYPE_POKE_APP_CTX:
      return sizeof(struct poke_app_ctx_msg);
      break;
    case MSG_TYPE_POKE_TAS_KERNEL:
      return sizeof(struct poke_tas_kernel_msg);
      break;
    case MSG_TYPE_POKE_TAS_CORE:
      return sizeof(struct poke_tas_core_msg);
      break;
    case MSG_TYPE_NEWAPP_REQ:
      return sizeof(struct newapp_req_msg);
      break;
    case MSG_TYPE_NEWAPP_RES:
      return sizeof(struct newapp_res_msg);
      break;
    default:
      fprintf(stderr, "channel_get_type_size: passed invalid message type.\n");
      return -1;
  }
}
