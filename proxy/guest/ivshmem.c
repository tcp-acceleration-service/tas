#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>

// #include "../../tas/lib/tas/internal.h"
#include "../proxy.h"
#include "../channel.h"
#include "internal.h"
#include "vfio.h"

int ivshmem_setup(struct guest_proxy *pxy);
int ivshmem_handle_msg(struct guest_proxy * pxy);

void ivshmem_notify_host(struct guest_proxy *pxy);
void ivshmem_drain_evfd(int fd);

int ivshmem_init(struct guest_proxy *pxy)
{
  void *tx_addr, *rx_addr;

  /* Create epoll that will wait for events from host */
  if ((pxy->epfd = epoll_create1(0)) < 0)
  {
    fprintf(stderr, "ivshmem_init: failed to create ivm_epfd.\n");
    return -1;
  }

  /* Set up vfio */
  if ((vfio_init(pxy)) < 0)
  {
    fprintf(stderr, "ivshmem_init: failed to init vfio device.\n");
    return -1;
  }

  /* Setup communication channel between guest and host */
  tx_addr = pxy->shm + CHAN_OFFSET;
  rx_addr = pxy->shm + CHAN_OFFSET + CHAN_SIZE;
  pxy->chan = channel_init(tx_addr, rx_addr, CHAN_SIZE);
  if (pxy->chan == NULL)
  {
      fprintf(stderr, "ivshmem_init: failed to init chan.\n");
      return -1;
  }

  /* Setup guest proxy with host proxy */
  if (ivshmem_setup(pxy) < 0)
  {
    fprintf(stderr, "ivshmem_init: failed handshake with host.\n");
    return -1;
  }

  // TODO: Add error handling here and close everything

  return 0;
}

int ivshmem_setup(struct guest_proxy *pxy)
{
  struct epoll_event evs[1];
  int ret;
  char *tasinfo = NULL;
  struct hello_msg *h_msg;
  struct tasinfo_req_msg treq_msg;
  struct tasinfo_res_msg tres_msg;

  /* Get number of cores from host */
  ret = channel_read(pxy->chan, &h_msg, sizeof(struct hello_msg));
  if (ret < sizeof(struct hello_msg))
  {
    fprintf(stderr, "ivshmem_handshake: failed to get number of cores.\n");
    return -1;
  }

  /* Send tasinfo request */
  treq_msg.msg_type = MSG_TYPE_TASINFO_REQ;
  ret = channel_write(pxy->chan, &treq_msg, sizeof(struct tasinfo_req_msg));
  ivshmem_notify_host(pxy);

  /* Receive tasinfo response */
  ret = epoll_wait(pxy->epfd, evs, 1, -1);
  if (ret < 0)
  {
    fprintf(stderr, "ivshmem_handshake: failed to receive"
        "tasinfo response.\n");
  }

  ivshmem_drain_evfd(pxy->irq_fd);
  ret = channel_read(pxy->chan, &tres_msg,
      sizeof(struct tasinfo_res_msg));
  if (ret < sizeof(*tasinfo))
  {
    fprintf(stderr, "ivshmem_handshake: failed to read tasinfo.\n");
  }

  pxy->flexnic_info = malloc(FLEXNIC_INFO_BYTES);
  if (pxy->flexnic_info == NULL)
  {
    fprintf(stderr, "ivshmem_handshake: failed to allocate flexnic_info.\n");
    return -1;
  }

  memcpy(pxy->flexnic_info, tasinfo, FLEXNIC_INFO_BYTES);

  /* Set proper offset and size of memory region */
  pxy->flexnic_info->dma_mem_off = pxy->shm_off;
  pxy->flexnic_info->dma_mem_size = pxy->shm_size;

  /* TODO: Create shm region for tas_info */

  return 0;
}

int ivshmem_poll(struct guest_proxy *pxy)
{
  int n, i;
  struct epoll_event events[2];
  n = epoll_wait(pxy->epfd, events, 2, 1000); 
  
  if (n > 0) 
  {
    for (i = 0; i < n; i++) 
    {
        ivshmem_drain_evfd(pxy->irq_fd);
        ivshmem_handle_msg(pxy);
    }
  }
  
  return n;
}

int flextcp_poll(struct guest_proxy *pxy)
{
  // TODO: Do poll for flextcp
  return 0;
}

int ivshmem_handle_msg(struct guest_proxy * pxy) {
  int ret;
  void *msg;
  uint8_t msg_type;
  size_t msg_size;

  msg_type = channel_get_msg_type(pxy->chan);
  msg_size = channel_get_type_size(msg_type);
  msg = malloc(msg_size);

  ret = channel_read(pxy->chan, msg, msg_size);
  if (ret <= 0)
  {
    fprintf(stderr, "ivshmem_handle_msg: channel read failed.\n");
    return -1;
  }

  switch(msg_type)
  {
    case MSG_TYPE_CONTEXT_RES:
      break;
    default:
      fprintf(stderr, "ivshmem_handle_msg: unknown message.\n");
  }

  return 0;
}

void ivshmem_notify_host(struct guest_proxy *pxy)
{
  /* Signal memory at offset 12 is the doorbell
     according to the ivshmem spec */
  volatile uint32_t *doorbell = (uint32_t *) (pxy->sgm + 12);
  *doorbell = (uint16_t) 0 | (uint16_t) HOST_PEERID << 16;
}

void ivshmem_drain_evfd(int fd)
{
  uint8_t buf[8];
  read(fd, buf, 8);
}
