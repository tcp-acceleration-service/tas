#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/eventfd.h>

#include "internal.h"
#include "flextcp.h"
#include "ivshmem.h"

int exited = 0;

struct guest_proxy *guest_init_proxy();

struct guest_proxy *guest_init_proxy()
{
  struct guest_proxy *pxy = malloc(sizeof(struct guest_proxy));
  
  pxy->chan_epfd = -1;
  pxy->irq_fd = -1;
  pxy->dev = -1;
  pxy->group = -1;
  pxy->cont = -1;
  
  pxy->shm = NULL;
  pxy->shm_size = 0;
  pxy->shm_off = 0;
  
  pxy->sgm = NULL;
  pxy->sgm_size = 0;
  pxy->sgm_off = 0;
  
  pxy->chan = NULL;
  
  pxy->flextcp_nfd = -1;
  pxy->flextcp_epfd = -1;
  pxy->flextcp_uxfd = -1;
  
  pxy->core_evfds = NULL;
  pxy->epfd = -1;
  pxy->ctxreq_id_next = 0;
  
  pxy->block_epfd = -1;
  pxy->block_elapsed = 0;
  pxy->poll_cycles_proxy = 10000;

  pxy->kernel_notifyfd = -1;
  pxy->next_app_id = 0;
  pxy->apps = NULL;
  
  pxy->flexnic_info = NULL;
  
  return pxy;
}

int main(int argc, char *argv[])
{
  int ret;
  unsigned int n;
  uint64_t start, end;
  struct epoll_event evs[1];
  struct guest_proxy *pxy = guest_init_proxy();

  if (ivshmem_init(pxy) < 0)
  {
    fprintf(stderr, "main: ivshmem_init failed.\n");
    return -1;
  }

  if (vflextcp_init(pxy) < 0)
  {
    fprintf(stderr, "main: flextcp_init failed.\n");
    return -1;
  }

  printf("running guest proxy.\n");  
  while (exited == 0)
  {
    n = 0;

    if (pxy->block_elapsed > pxy->poll_cycles_proxy)
    {
      epoll_wait(pxy->block_epfd, evs, 1, -1);
    }

    start = util_rdtsc();

    if ((ret = ivshmem_channel_poll(pxy)) > 0)
      n += ret;

    if ((ret = vflextcp_poll(pxy)) > 0)
      n += ret;    

    if (n > 0)
    {
        pxy->block_elapsed = 0;
    } else {
        end = util_rdtsc();
        pxy->block_elapsed += end - start;
    }
  }

  return 0;
}
