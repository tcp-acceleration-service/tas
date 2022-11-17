#ifndef IVSHMEM_H_
#define IVSHMEM_H_

#include "internal.h"

int ivshmem_init(struct guest_proxy *ctx);
int ivshmem_poll(struct guest_proxy *pxy);
void ivshmem_notify_host(struct guest_proxy *pxy);
void ivshmem_drain_evfd(int fd);

#endif /* ndef IVSHMEM_H_ */