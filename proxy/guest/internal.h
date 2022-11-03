#ifndef GUEST_INTERNAL_H_
#define GUEST_INTERNAL_H_

#include <stdint.h>
#include <unistd.h>

#include "../channel.h"

struct guest_proxy {
    /* Epoll fd for all guest to host comm */
    int epfd;

    /* IN IVM_EPFD INTEREST LIST */
    /* Fd for vfio interrupt requests */
    int irq_fd;

    /* Vfio fds */
    int dev;
    int group;
    int cont;

    /* Shared memory region between guest and TAS */
    uint8_t *shm;
    size_t shm_size;
    size_t shm_off;

    /* Inter-vm signal memory used for interrupts */
    uint8_t *sgm;
    size_t sgm_size;
    size_t sgm_off;

    /* Channel used for vm communication */
    struct channel *chan;

    struct flexnic_info *flexnic_info;
};

#endif /* ndef INTERNAL_H_ */