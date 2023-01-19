#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "internal.h"
#include "flextcp.h"
#include "ivshmem.h"

int exited = 0;

struct guest_proxy *guest_init_proxy();

struct guest_proxy *guest_init_proxy()
{
  struct guest_proxy *pxy = malloc(sizeof(struct guest_proxy));
  
  pxy->epfd = -1;
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
  
  pxy->vfds = NULL;
  pxy->vepfd = -1;
  pxy->ctxreq_id_next = 0;
  
  pxy->kernel_notifyfd = -1;
  pxy->next_app_id = 0;
  pxy->apps = NULL;
  
  pxy->flexnic_info = NULL;
  
  return pxy;
}

int main(int argc, char *argv[])
{
  unsigned int n;
  struct guest_proxy *pxy = guest_init_proxy();

  if ((pxy->kernel_notifyfd = epoll_create1(0)) < 0)
  {
    fprintf(stderr, "main: failed to create kernel_notifyfd.\n");
  }

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

  fprintf(stdout, "running guest proxy.\n");  
  while (exited == 0)
  {
    n = 0;
    n += ivshmem_poll(pxy);
    n += vflextcp_poll(pxy);
  }

  return 0;
}
