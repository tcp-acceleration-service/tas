#ifndef HOST_INTERNAL_H_
#define HOST_INTERNAL_H_

#include <tas_pxy.h>

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
    /* List of context requests for VM */
    struct vmcontext_req *ctxs;

};

struct host_proxy {
/* Epoll fd that subscribes to uxfd */
  int epfd;
  int ctx_epfd;
  int chan_epfd;
  int block_epfd;
  int uxfd;
  int next_vm_id;
  int next_app_id[FLEXNIC_PL_VMST_NUM];
  struct v_machine vms[10];

  /* Used to sleep proxy */
  uint64_t block_elapsed;
  uint64_t poll_cycles_proxy;
};

#endif /* ndef HOST_INTERNAL_H_ */