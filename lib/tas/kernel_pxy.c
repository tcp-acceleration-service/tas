
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <tas.h>
#include <tas_ll.h>
#include <tas_memif.h>
#include <kernel_appif.h>
#include <tas_pxy.h>

#include "internal.h"

#define NIC_RXQ_LEN (64 * 32 * 1024)
#define NIC_TXQ_LEN (64 * 8192)

static int ksock_fd_vm = -1;
static int ksock_fd_app[FLEXNIC_PL_VMST_NUM][FLEXNIC_PL_APPST_NUM];

/* TODO: Only need one kernel evfd_pxy */
static int kernel_evfd_pxy[FLEXNIC_PL_VMST_NUM];

int flextcp_vm_kernel_connect(int vmid, int *shmfd, int *flexnic_evfd);

int flextcp_proxy_kernel_connect(int *shmfds, int *flexnic_evfd)
{
  int ret;
  uint16_t vmid;

  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM - 1; vmid++)
  {
    ret = flextcp_vm_kernel_connect(vmid, &shmfds[vmid], flexnic_evfd);
    if (ret < 0)
    {
      fprintf(stderr, "flextcp_proxy_kernel_connect: "
          "failed to connect vm=%d.\n", vmid);
      return -1;
    }
  }

  return 0;
}

int flextcp_vm_kernel_connect(int vmid, int *shmfd, int *flexnic_evfd)
{
  int fd, *pfd;
  uint8_t b;
  ssize_t r;
  uint32_t num_fds, off, i, n;
  struct sockaddr_un saun;
  struct cmsghdr *cmsg;

  /* prepare socket address */
  memset(&saun, 0, sizeof(saun));
  saun.sun_family = AF_UNIX;
  memcpy(saun.sun_path, KERNEL_SOCKET_PATH, sizeof(KERNEL_SOCKET_PATH));

  if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) == -1) {
    perror("flextcp_vm_kernel_connect: socket failed");
    return -1;
  }

  if (connect(fd, (struct sockaddr *) &saun, sizeof(saun)) != 0) {
    perror("flextcp_vm_kernel_connect: connect failed");
    return -1;
  }

  struct iovec iov = {
    .iov_base = &num_fds,
    .iov_len = sizeof(uint32_t),
  };

  union {
    char buf[CMSG_SPACE(sizeof(int) * 4)];
    struct cmsghdr align;
  } u;

  struct msghdr msg = {
    .msg_name = NULL,
    .msg_namelen = 0,
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = u.buf,
    .msg_controllen = sizeof(u.buf),
    .msg_flags = 0,
  };

  if (flextcp_kernel_get_notifyfd(fd, &num_fds, kernel_evfd_pxy) != 0)
  {
    fprintf(stderr, "flextcp_vm_kernel_connect: failed to receive notify fd.\n");
    return -1;
  }

  if (flextcp_kernel_get_shmfd(fd, shmfd) != 0)
  {
    fprintf(stderr, "flextcp_vm_kernel_connect: failed to receive shm fd.\n");
    return -1;
  }

  /* receive fast path fds in batches of 4 */
  off = 0;
  for (off = 0 ; off < num_fds; ) {
    iov.iov_base = &b;
    iov.iov_len = 1;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u);

    /* receive fd message (up to 4 fds at once) */
    if ((r = recvmsg(fd, &msg, 0)) != 1) {
      fprintf(stderr, "flextcp_vm_kernel_connect: recvmsg fd failed (%zd).\n", r);
      abort();
    }

    n = (num_fds - off >= 4 ? 4 : num_fds - off);

    /* get kernel fd from welcome message */
    cmsg = CMSG_FIRSTHDR(&msg);
    pfd = (int *) CMSG_DATA(cmsg);
    if (msg.msg_controllen <= 0 || cmsg->cmsg_len != CMSG_LEN(sizeof(int) * n)) {
      fprintf(stderr, "flextcp_vm_kernel_connect: accessing ancillary data fds "
          "failed.\n");
      abort();
    }

    for (i = 0; i < n; i++) {
      flexnic_evfd[off++] = pfd[i];
    }
  }
  
  ksock_fd_vm = fd;
  return 0;
}

int flextcp_proxy_kernel_newapp(int vmid, int appid)
{
  int fd;
  struct sockaddr_un saun;

  /* prepare socket address */
  memset(&saun, 0, sizeof(saun));
  saun.sun_family = AF_UNIX;
  snprintf(saun.sun_path, sizeof(saun.sun_path), "%s_vm_%d",
      KERNEL_SOCKET_PATH, vmid);

  if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) == -1) {
    perror("flextcp_vm_kernel_connect: socket failed");
    return -1;
  }

  if (connect(fd, (struct sockaddr *) &saun, sizeof(saun)) != 0) {
    perror("flextcp_vm_kernel_connect: connect failed");
    return -1;
  }

  ksock_fd_app[vmid][appid] = fd;

  return 0;
}

int flextcp_proxy_kernel_newctx(struct flextcp_context *ctx,
    uint8_t *presp, ssize_t *presp_sz, int vmid, int appid)
{
  ssize_t sz, off, total_sz;
  struct kernel_uxsock_response *resp;
  uint8_t resp_buf[sizeof(*resp) +
      FLEXTCP_MAX_FTCPCORES * sizeof(resp->flexnic_qs[0])];
  struct kernel_uxsock_request req = {
      .rxq_len = NIC_RXQ_LEN,
      .txq_len = NIC_TXQ_LEN,
    };
  uint16_t i;

  /* send request on kernel socket */
  struct iovec iov = {
    .iov_base = &req,
    .iov_len = sizeof(req),
  };
  union {
    char buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr align;
  } u;
  struct msghdr msg = {
    .msg_name = NULL,
    .msg_namelen = 0,
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = u.buf,
    .msg_controllen = sizeof(u.buf),
    .msg_flags = 0,
  };
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  int *myfd = (int *)CMSG_DATA(cmsg);
  *myfd = ctx->evfd;
  sz = sendmsg(ksock_fd_app[vmid][appid], &msg, 0);
  assert(sz == sizeof(req));

  /* receive response on kernel socket */
  resp = (presp == NULL) ? 
    (struct kernel_uxsock_response *) resp_buf : 
    (struct kernel_uxsock_response *) presp;
  off = 0;
  while (off < sizeof(*resp)) {
    sz = read(ksock_fd_app[vmid][appid], (uint8_t *) resp + off, sizeof(*resp) - off);
    if (sz < 0) {
      perror("flextcp_proxy_kernel_newctx: read failed");
      return -1;
    }
    off += sz;
  }

  if (resp->flexnic_qs_num > FLEXTCP_MAX_FTCPCORES) {
    fprintf(stderr, "flextcp_proxy_kernel_newctx: stack only supports up to %u "
        "queues, got %u.\n", FLEXTCP_MAX_FTCPCORES, resp->flexnic_qs_num);
    abort();
  }

  /* receive queues in response */
  total_sz = sizeof(*resp) + resp->flexnic_qs_num * sizeof(resp->flexnic_qs[0]);
  while (off < total_sz) {
    sz = read(ksock_fd_app[vmid][appid], (uint8_t *) resp + off, total_sz - off);
    if (sz < 0) {
      perror("flextcp_proxy_kernel_newctx: read failed");
      return -1;
    }
    off += sz;
  }

  if (resp->status != 0) {
    fprintf(stderr, "flextcp_proxy_kernel_newctx: request failed.\n");
    return -1;
  }

  if (presp != NULL) {
    assert(presp_sz != NULL);
    *presp_sz = total_sz;
    return 0;
  }

  /* Use flexnic_mem_pxy[0] for the host proxy, so VMs start with ID=1 */
  /* fill in ctx struct */
  ctx->kin_base = (uint8_t *) flexnic_mem_pxy[vmid] + resp->app_out_off;
  ctx->kin_len = resp->app_out_len / sizeof(struct kernel_appout);
  ctx->kin_head = 0;

  ctx->kout_base = (uint8_t *) flexnic_mem_pxy[vmid] + resp->app_in_off;
  ctx->kout_len = resp->app_in_len /  sizeof(struct kernel_appin);
  ctx->kout_head = 0;

  ctx->db_id = resp->flexnic_db_id;
  ctx->num_queues = resp->flexnic_qs_num;
  ctx->next_queue = 0;

  ctx->rxq_len = NIC_RXQ_LEN;
  ctx->txq_len = NIC_TXQ_LEN;

  for (i = 0; i < resp->flexnic_qs_num; i++) {
    ctx->queues[i].rxq_base =
      (uint8_t *) flexnic_mem_pxy[vmid] + resp->flexnic_qs[i].rxq_off;
    ctx->queues[i].txq_base =
      (uint8_t *) flexnic_mem_pxy[vmid] + resp->flexnic_qs[i].txq_off;

    ctx->queues[i].rxq_head = 0;
    ctx->queues[i].txq_tail = 0;
    ctx->queues[i].txq_avail = ctx->txq_len;
    ctx->queues[i].last_ts = 0;
  }

  return 0;
}