/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <kernel_appif.h>
#include <utils_timeout.h>
#include "internal.h"

#define NIC_RXQ_LEN (64 * 32 * 1024)
#define NIC_TXQ_LEN (64 * 8192)

static int ksock_fd = -1;
static int kernel_evfd = 0;

void flextcp_kernel_kick(void)
{
  static uint64_t __thread last_ts = 0;
  uint64_t now = util_rdtsc();

  /* fprintf(stderr, "kicking kernel?\n"); */

  if(now - last_ts > flexnic_info->poll_cycle_tas) {
    // Kick kernel
    /* fprintf(stderr, "kicking kernel\n"); */
    assert(kernel_evfd != 0);
    uint64_t val = 1;
    int r = write(kernel_evfd, &val, sizeof(uint64_t));
    assert(r == sizeof(uint64_t));
  }

  last_ts = now;
}

int flextcp_kernel_connect(void)
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
    perror("flextcp_kernel_connect: socket failed");
    return -1;
  }

  if (connect(fd, (struct sockaddr *) &saun, sizeof(saun)) != 0) {
    perror("flextcp_kernel_connect: connect failed");
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

  /* receive welcome message:
   *   contains the fd for the kernel, and the count of flexnic fds */
  if ((r = recvmsg(fd, &msg, 0)) != sizeof(uint32_t)) {
    fprintf(stderr, "flextcp_kernel_connect: recvmsg failed (%zd)\n", r);
    abort();
  }

  /* get kernel fd from welcome message */
  cmsg = CMSG_FIRSTHDR(&msg);
  pfd = (int *) CMSG_DATA(cmsg);
  if (msg.msg_controllen <= 0 || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
    fprintf(stderr, "flextcp_kernel_connect: accessing ancillary data "
        "failed\n");
    abort();
  }
  kernel_evfd = *pfd;

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
      fprintf(stderr, "flextcp_kernel_connect: recvmsg fd failed (%zd)\n", r);
      abort();
    }

    n = (num_fds - off >= 4 ? 4 : num_fds - off);

    /* get kernel fd from welcome message */
    cmsg = CMSG_FIRSTHDR(&msg);
    pfd = (int *) CMSG_DATA(cmsg);
    if (msg.msg_controllen <= 0 || cmsg->cmsg_len != CMSG_LEN(sizeof(int) * n)) {
      fprintf(stderr, "flextcp_kernel_connect: accessing ancillary data fds "
          "failed\n");
      abort();
    }

    for (i = 0; i < n; i++) {
      flexnic_evfd[off++] = pfd[i];
    }
  }

  ksock_fd = fd;
  return 0;
}

int flextcp_kernel_newctx(struct flextcp_context *ctx)
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
  sz = sendmsg(ksock_fd, &msg, 0);
  assert(sz == sizeof(req));

  /* receive response on kernel socket */
  resp = (struct kernel_uxsock_response *) resp_buf;
  off = 0;
  while (off < sizeof(*resp)) {
    sz = read(ksock_fd, (uint8_t *) resp + off, sizeof(*resp) - off);
    if (sz < 0) {
      perror("flextcp_kernel_newctx: read failed");
      return -1;
    }
    off += sz;
  }

  if (resp->flexnic_qs_num > FLEXTCP_MAX_FTCPCORES) {
    fprintf(stderr, "flextcp_kernel_newctx: stack only supports up to %u "
        "queues, got %u\n", FLEXTCP_MAX_FTCPCORES, resp->flexnic_qs_num);
    abort();
  }
  /* receive queues in response */
  total_sz = sizeof(*resp) + resp->flexnic_qs_num * sizeof(resp->flexnic_qs[0]);
  while (off < total_sz) {
    sz = read(ksock_fd, (uint8_t *) resp + off, total_sz - off);
    if (sz < 0) {
      perror("flextcp_kernel_newctx: read failed");
      return -1;
    }
    off += sz;
  }

  if (resp->status != 0) {
    fprintf(stderr, "flextcp_kernel_newctx: request failed\n");
    return -1;
  }

  /* fill in ctx struct */
  ctx->kin_base = (uint8_t *) flexnic_mem + resp->app_out_off;
  ctx->kin_len = resp->app_out_len / sizeof(struct kernel_appout);
  ctx->kin_head = 0;

  ctx->kout_base = (uint8_t *) flexnic_mem + resp->app_in_off;
  ctx->kout_len = resp->app_in_len /  sizeof(struct kernel_appin);
  ctx->kout_head = 0;

  ctx->db_id = resp->flexnic_db_id;
  ctx->num_queues = resp->flexnic_qs_num;
  ctx->next_queue = 0;

  ctx->rxq_len = NIC_RXQ_LEN;
  ctx->txq_len = NIC_TXQ_LEN;

  for (i = 0; i < resp->flexnic_qs_num; i++) {
    ctx->queues[i].rxq_base =
      (uint8_t *) flexnic_mem + resp->flexnic_qs[i].rxq_off;
    ctx->queues[i].txq_base =
      (uint8_t *) flexnic_mem + resp->flexnic_qs[i].txq_off;

    ctx->queues[i].rxq_head = 0;
    ctx->queues[i].txq_tail = 0;
    ctx->queues[i].txq_avail = ctx->txq_len;
    ctx->queues[i].last_ts = 0;
  }

  return 0;
}

int flextcp_kernel_reqscale(struct flextcp_context *ctx, uint32_t cores)
{
  uint32_t pos = ctx->kin_head;
  struct kernel_appout *kin = ctx->kin_base;

  kin += pos;

  if (kin->type != KERNEL_APPOUT_INVALID) {
    fprintf(stderr, "flextcp_kernel_reqscale: no queue space\n");
    return -1;
  }

  kin->data.req_scale.num_cores = cores;
  MEM_BARRIER();
  kin->type = KERNEL_APPOUT_REQ_SCALE;
  flextcp_kernel_kick();

  pos = pos + 1;
  if (pos >= ctx->kin_len) {
    pos = 0;
  }
  ctx->kin_head = pos;

  return 0;
}
