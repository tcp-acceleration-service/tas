#ifndef IVSHMEM_H_
#define IVSHMEM_H_

#include "internal.h"

int ivshmem_init(struct guest_proxy *ctx);
int ivshmem_poll();
int flextcp_poll();

#endif /* ndef IVSHMEM_H_ */