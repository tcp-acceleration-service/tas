#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>

#include "internal.h"
#include "vfio.h"
#include "ivshmem.h"
#include "flextcp.h"
#include "../proxy.h"
#include "../channel.h"

static int channel_handle_hello(struct guest_proxy *pxy);
static int channel_handle_tasinfo_res(struct guest_proxy *pxy,
    struct tasinfo_res_msg *msg);
static int channel_handle_newapp_res(struct guest_proxy *pxy,
    struct newapp_res_msg *msg);
static int channel_handle_ctx_res(struct guest_proxy *pxy, 
    struct context_res_msg *msg);
static int channel_handle_vpoke(struct guest_proxy *pxy, struct poke_app_ctx_msg *msg);

int ivshmem_init(struct guest_proxy *pxy)
{
  void *tx_addr, *rx_addr;

  /* Create epoll that will wait for events from host */
  if ((pxy->chan_epfd = epoll_create1(0)) < 0)
  {
    fprintf(stderr, "ivshmem_init: failed to create chan_epfd.\n");
    return -1;
  }

  /* Create epoll that will wait and block */
  if ((pxy->block_epfd = epoll_create1(0)) < 0)
  {
    fprintf(stderr, "ivshmem_init: failed to create block_epfd.\n");
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

  // TODO: Add error handling here and close everything

  return 0;
}

void ivshmem_notify_host(struct guest_proxy *pxy)
{
  /* Signal memory at offset 12 is the doorbell
     according to the ivshmem spec */
  volatile uint32_t *doorbell = (uint32_t *) (pxy->sgm + 12);
  *doorbell = (uint16_t) 0 | (uint16_t) HOST_PEERID << 16;
}

/* Clears the interrupt status register. If register is not cleared
   we keep receiving interrupt events from epoll. */
int ivshmem_drain_evfd(int fd) 
{
  int ret;
  uint8_t buf[8];
  ret = read(fd, buf, 8);
  if (ret == 0)
  {
    fprintf(stderr, "ivshmem_drain_evfd: failed to drain evfd.\n");
    return -1;
  }

  return 0;
}

int ivshmem_channel_poll(struct guest_proxy *pxy)
{
  int ret, is_empty;
  void *msg;
  uint8_t msg_type;
  size_t msg_size;
  
  /* Return if there are no messages in channel */
  MEM_BARRIER();
  shmring_lock(pxy->chan->rx);
  is_empty = shmring_is_empty(pxy->chan->rx);
  shmring_unlock(pxy->chan->rx);

  MEM_BARRIER();
  if (is_empty)
  {
    return 0;
  }

  msg_type = channel_get_msg_type(pxy->chan);
  msg_size = channel_get_type_size(msg_type);
  msg = malloc(msg_size);

  ret = channel_read(pxy->chan, msg, msg_size);
  if (ret == 0)
  {
    fprintf(stderr, "ivshmem_channel_poll: channel read failed.\n");
    return -1;
  }

  switch(msg_type)
  {
    case MSG_TYPE_HELLO:
      channel_handle_hello(pxy);
      break;
    case MSG_TYPE_TASINFO_RES:
      channel_handle_tasinfo_res(pxy, (struct tasinfo_res_msg *) msg);
      break;
    case MSG_TYPE_NEWAPP_RES:
      channel_handle_newapp_res(pxy, (struct newapp_res_msg *) msg);
      break;
    case MSG_TYPE_CONTEXT_RES:
      channel_handle_ctx_res(pxy, (struct context_res_msg *) msg);
      break;
    case MSG_TYPE_POKE_APP_CTX:
      channel_handle_vpoke(pxy, (struct poke_app_ctx_msg *) msg);
      break;
    default:
      fprintf(stderr, "ivshmem_channel_poll: unknown message.\n");
  }

  return 1;
}

static int channel_handle_hello(struct guest_proxy *pxy)
{
  int ret;
  struct tasinfo_req_msg treq_msg;

  /* Send tasinfo request and wait for the response in the channel poll */
  treq_msg.msg_type = MSG_TYPE_TASINFO_REQ;
  ret = channel_write(pxy->chan, &treq_msg, sizeof(struct tasinfo_req_msg));
  if (ret != sizeof(struct tasinfo_req_msg))
  {
    fprintf(stderr, "channel_handle_hello: failed to write tasinfo req msg.\n");
    return -1;
  }

  ivshmem_notify_host(pxy);

  return 0;
}

static int channel_handle_tasinfo_res(struct guest_proxy *pxy, struct tasinfo_res_msg *msg)
{
  int ret;

  pxy->flexnic_info = malloc(FLEXNIC_INFO_BYTES);
  if (pxy->flexnic_info == NULL)
  {
    fprintf(stderr, "channel_handle_tasinfo_res: failed to allocate flexnic_info.\n");
    return -1;
  }

  memcpy(pxy->flexnic_info, msg->flexnic_info, FLEXNIC_INFO_BYTES);

  /* Set proper offset and size of memory region */
  pxy->flexnic_info->dma_mem_off = pxy->shm_off;
  pxy->flexnic_info->dma_mem_size = pxy->shm_size;
  
  /* Create shm region */
  ret = vflextcp_serve_tasinfo((uint8_t *) pxy->flexnic_info, 
      FLEXNIC_INFO_BYTES);
  if (ret < 0)
  {
    fprintf(stderr, "channel_handle_tasinfo_res: failed to create tas_info shm region.\n");
    return -1; 
  }

  /* Init epfd used to listen to core_evfds and kernel notify fd */
  pxy->epfd = epoll_create1(0);
  if ((pxy->epfd = epoll_create1(0)) < 0)
  {
    fprintf(stderr,
            "channel_handle_tasinfo_res: failed to create fd for vepfd.\n");
    return -1;
  }

  if (vflextcp_kernel_notifyfd_init(pxy) != 0)
  {
    fprintf(stderr, "channel_handle_tasinfo_res: "
            "vflextcp_kernel_notifyfd_init failed.\n");
  }
  
  if (vflextcp_core_evfds_init(pxy) != 0) 
  {
    fprintf(stderr, "channel_handle_tasinfo_res: "
            "vflextcp_core_evfds_init failed.\n");
  }


  return 0;
}

static int channel_handle_newapp_res(struct guest_proxy *pxy,
    struct newapp_res_msg *msg)
{
  vflextcp_handle_newapp_res(pxy, msg);
  return 0;
}

static int channel_handle_ctx_res(struct guest_proxy *pxy, struct context_res_msg *msg)
{
  uint32_t app_id;

  app_id = msg->app_id;
  vflextcp_write_context_res(pxy, app_id, msg->resp, msg->resp_size);

  return 0;
}

static int channel_handle_vpoke(struct guest_proxy *pxy, struct poke_app_ctx_msg *msg)
{
  vflextcp_poke(pxy, msg->ctxreq_id);
  return 0;
}