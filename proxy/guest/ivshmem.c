#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>

#include "../../tas/lib/tas/internal.h"
#include "../proxy.h"
#include "../channel.h"
#include "internal.h"
#include "vfio.h"

int ivshmem_init(struct guest_proxy *pxy)
{
  struct channel *chan;
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
  if (ivshmem_setup_with_host(pxy) < 0)
  {
    fprintf(stderr, "ivshmem_init: failed handshake with host.\n");
    return -1;
  }

  // TODO: Add error handling here and close everything

  return 0;
}

int ivshmem_poll(struct guest_proxy *pxy)
{
  // TODO: Do poll for ivshmem
}

int flextcp_poll(struct guest_proxy *pxy)
{
  // TODO: Do poll for flextcp
}

int ivshmem_setup_with_host(struct guest_proxy *pxy)
{
  struct epoll_event evs[1];
  int n_cores, ret, t_req;
  char *tasinfo;
  void *buf;

  /* Get number of cores from host */
  ret = channel_read(pxy->chan, &n_cores, sizeof(n_cores));
  if (ret == 0)
  {
    fprintf(stderr, "ivshmem_handshake: failed to get number of cores.\n");
    return -1;
  }

  /* Send tasinfo request */
  t_req = 1;
  ret = channel_write(pxy->chan, t_req, sizeof(t_req));

  /* Receive tasinfo response */
  ret = epoll_wait(pxy->epfd, evs, 1, -1);
  if (ret <= 0)
  {
    fprintf(stderr, "ivshmem_handshake: failed to receive"
        "tasinfo response.\n");
  }

  ret = channel_read(pxy->chan, tasinfo, sizeof(*tasinfo));
  if (ret <= 0)
  {
    fprintf(stderr, "ivshmem_handshake: failed to read tasinfo.\n");
  }

  /* Copy tasinfo response into flexnic_info */
  pxy->flexnic_info = malloc(FLEXNIC_INFO_BYTES);
  if (pxy->flexnic_info == NULL)
  {
    fprintf(stderr, "ivshmem_handshake: failed to allocate flexnic_info.\n");
    return -1;
  }

  memcpy(pxy->flexnic_info, tasinfo, tasinfo_len);

  /* Set proper offset and size of memory region */
  // pxy->flexnic_info->dma_mem_off = pxy->shm_off;
  pxy->flexnic_info->dma_mem_size = pxy->shm_size;

  /* Create shm region for tas_info */

  return 0;
}