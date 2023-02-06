#include <stdlib.h>
#include <stdio.h>

#include <tas_pxy.h>
#include "ivshmem.h"

int exited = 0;

int main(int argc, char *argv[])
{
    /* Connect to tas and get shmfd, kernel_evfd and core_evfds */
    if (flextcp_proxy_init() != 0)
    {
        fprintf(stderr, "main: flextcp_init failed.\n");
        return -1;
    }

    /* Init ivshmem */
    if (ivshmem_init() != 0)
    {
        fprintf(stderr, "main: ivshmem_init failed.\n");
        return -1;
    }

    /* Poll ivshmem */
    printf("running host proxy.\n");
    while(exited == 0)
    {
        ivshmem_poll();
    }

}