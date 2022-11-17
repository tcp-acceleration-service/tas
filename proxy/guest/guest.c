#include <stdio.h>

#include "internal.h"
#include "flextcp.h"
#include "ivshmem.h"

int exited = 0;

int main(int argc, char *argv[])
{
  unsigned int n;
  struct guest_proxy pxy;

  fprintf(stdout, "running guest proxy.\n");  

  if ((pxy.kernel_notifyfd = epoll_create1(0)) < 0)
  {
    fprintf(stderr, "main: failed to create kernel_notifyfd.\n");
  }

  if (ivshmem_init(&pxy) < 0)
  {
    fprintf(stderr, "main: ivshmem_init failed.\n");
    return -1;
  }

  if (vflextcp_init(&pxy) < 0)
  {
    fprintf(stderr, "main: flextcp_init failed.\n");
    return -1;
  }

  while (exited == 0)
  {
    n = 0;
    n += ivshmem_poll(&pxy);
    n += vflextcp_poll(&pxy);
  }

  return 0;

}