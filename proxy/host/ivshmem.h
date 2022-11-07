#ifndef PROXY_IVSHMEM_H_
#define PROXY_IVSHMEM_H_

#include "../channel.h"

#define IVSHMEM_SOCK_PATH "/run/tasproxy"

/* These variables are all pre-determined by QEMU
   to get ivshmem to work */
#define IVSHMEM_PROTOCOL_VERSION 0
#define HOST_PEERID 255

#define MAX_VMS 16

struct v_machine {
    int id;  
    /* Connection fd */
    int fd;
    /* Interrupt fd */
    int ifd;
    /* Notify fd  */
    int nfd;
    /* Shared memory channel used for tx and rx */
    struct channel *chan;

};

int ivshmem_init();
int ivshmem_poll();

#endif /* ndef PROXY_IVSHMEM_H_ */