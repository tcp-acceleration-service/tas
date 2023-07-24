#ifndef PROXY_IVSHMEM_H_
#define PROXY_IVSHMEM_H_

#include "internal.h"
#include "../channel.h"

#define IVSHMEM_SOCK_PATH "/run/tasproxy"

/* These variables are all pre-determined by QEMU
   to get ivshmem to work */
#define IVSHMEM_PROTOCOL_VERSION 0

#define MAX_VMS 16

struct vmcontext_req {
    int app_id;
    uint32_t ctxreq_id;
    int cfd; 
    struct v_machine *vm;
    struct flextcp_context *ctx;
    struct vmcontext_req *next;
};

int ivshmem_init(struct host_proxy *pxy);
int ivshmem_poll(struct host_proxy *pxy);

#endif /* ndef PROXY_IVSHMEM_H_ */