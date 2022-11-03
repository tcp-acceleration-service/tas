#include <stdlib.h>
#include <stdio.h>

#include <tas_ll.h>
#include "ivshmem.h"

int exited = 0;

int main(int argc, char *argv[])
{
    /* Connect to tas and get shmfd, kernel_evfd and core_evfds */
    if (flextcp_init() != 0)
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
    fprintf(stdout, "running host proxy.\n");
    while(exited == 0)
    {
        ivshmem_poll();
    }

}