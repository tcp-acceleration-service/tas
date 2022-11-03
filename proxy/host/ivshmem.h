#ifndef PROXY_IVSHMEM_H_
#define PROXY_IVSHMEM_H_

#include "../channel.h"

#define IVSHMEM_SOCK_PATH "/run/tasproxy"
#define PEERID_LOCAL 0
#define PEERID_VM 1

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