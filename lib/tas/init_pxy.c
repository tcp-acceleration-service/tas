#include <stdlib.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <string.h>

#include <tas.h>
#include <tas_ll.h>
#include <tas_ll_connect.h>
#include <tas_pxy.h>

#include "internal.h"

/* FD for shm mem of each VM */
int flexnic_shmfds_pxy[FLEXNIC_PL_VMST_NUM];
/* SHM region for each VM */
void *flexnic_mem_pxy[FLEXNIC_PL_VMST_NUM];
/* Info shared memory region */
struct flexnic_info *flexnic_info_pxy = NULL;
/* Eventfd for different cores */
int flexnic_evfd_pxy[FLEXTCP_MAX_FTCPCORES];

int flextcp_proxy_init()
{
  int ret;

  if ((flextcp_proxy_kernel_connect(flexnic_shmfds_pxy, flexnic_evfd_pxy)) < 0) 
  {
    fprintf(stderr, "flextcp_proxy_init: connecting to kernel failed.\n");
    return -1;
  }

  ret = flexnic_driver_connect_mult(&flexnic_info_pxy, flexnic_mem_pxy,
      flexnic_shmfds_pxy);

  if (ret < 0)
  {
    fprintf(stderr, "flextcp_proxy_init: failed to connect to all vm fds.\n");
    return -1;
  }

  return 0;
}

int flextcp_proxy_context_create(struct flextcp_context *ctx,
    uint8_t *presp, ssize_t *presp_sz, int vmid)
{
  static uint16_t ctx_id = 0;

  memset(ctx, 0, sizeof(*ctx));

  ctx->ctx_id = __sync_fetch_and_add(&ctx_id, 1);
  if (ctx->ctx_id >= FLEXTCP_MAX_CONTEXTS) 
  {
    fprintf(stderr, "flextcp_proxy_context_create: maximum number of contexts "
        "exeeded.\n");
    return -1;
  }

  ctx->evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (ctx->evfd < 0) 
  {
    perror("flextcp_proxy_context_create: eventfd for waiting fd failed");
    return -1;
  }

  return flextcp_proxy_kernel_newctx(ctx, presp, presp_sz, vmid);
}