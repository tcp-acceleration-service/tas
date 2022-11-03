#include <stdio.h>

#include "guest/internal.h"
#include "guest/ivshmem.h"

int exited = 0;

int main(int argc, char *argv[])
{
  unsigned int n;
  struct guest_proxy pxy;

  fprintf(stdout, "running guest proxy.\n");  

  if (ivshmem_init(&pxy) < 0)
  {
    fprintf(stderr, "main: ivshmem_init failed.\n");
    return -1;
  }

  // if (flextcp_init() < 0)
  // {
  //   fprintf(stderr, "main: flextcp_init failed.\n");
  //   return -1;
  // }

  while (exited == 0)
  {
    n = 0;
    n += ivshmem_poll(&pxy);
    // n += flextcp_poll(pxy);
  }

  return 0;

}