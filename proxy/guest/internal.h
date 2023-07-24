#ifndef GUEST_INTERNAL_H_
#define GUEST_INTERNAL_H_

#include <stdint.h>
#include <unistd.h>
#include <sys/epoll.h>

#include "../../include/tas_memif.h"
#include "../../include/kernel_appif.h"
#include "../channel.h"

#define MAX_CONTEXT_REQ 100

struct proxy_application {
  int fd;
  size_t req_rx;
  struct proxy_context_req proxy_req;
  struct proxy_application *next;

  uint16_t id;
  uint8_t closed;
};

struct guest_proxy {
    /* Epoll fd for all guest to host comm */
    int chan_epfd;
    
    /* Fd for vfio interrupt requests */
    int irq_fd;
    /* VFIO fds */
    int dev;
    int group;
    int cont;
    
    /* Shared memory region between guest and TAS */
    void *shm;
    size_t shm_size;
    size_t shm_off;
    
    /* Inter-vm signal memory used for interrupts */
    void *sgm;
    size_t sgm_size;
    size_t sgm_off;
    
    /* Channel used for vm communication */
    struct channel *chan;
    
    /* Flextcp */
    int flextcp_nfd;
    int flextcp_epfd;
    int flextcp_uxfd;
    
    /* List of eventfds for fastpath cores and epoll */
    int *core_evfds;
    int epfd;
    int ctxreq_id_next;

    /* Fields used to block the proxy */
    int block_epfd;
    uint64_t block_elapsed;
    uint64_t poll_cycles_proxy;
    
    /* Notify fd for kernel slowpath */
    int kernel_notifyfd;
    
    /* Applications */
    uint16_t next_app_id;
    struct proxy_application *apps;
    struct proxy_context_req *context_reqs[MAX_CONTEXT_REQ];

    struct flexnic_info *flexnic_info;
};

#endif /* ndef GUEST_INTERNAL_H_ */