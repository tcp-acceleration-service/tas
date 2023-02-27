#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <fastpath.h>

static int uxsocket_send_kernel_notifyfd(int cfd, int cores_num,
    int kernel_notifyfd);
static int uxsocket_send_core_fds(int cfd, int cores_num);
static int uxsocket_send_shm_fd(int cfd, int shmfd);

int appif_connect_accept(int cfd, int cores_num, 
    int kernel_notifyfd, int shm_fd) 
{

  if (uxsocket_send_kernel_notifyfd(cfd, cores_num, kernel_notifyfd) != 0) 
  {
    fprintf(stderr, "appif_connect_accept: failed to send notify_kernelfd.\n");
    return -1;
  }

  if (uxsocket_send_shm_fd(cfd, shm_fd) != 0) 
  {
    fprintf(stderr, "appif_connect_accept: failed to send shm fd.\n");
    return -1;
  }

  if (uxsocket_send_core_fds(cfd, cores_num) != 0) 
  {
    fprintf(stderr, "appif_connect_accept: failed to send core fds.\n");
    return -1;
  }

  return 0;
}

int uxsocket_send_kernel_notifyfd(int cfd, int cores_num, int kernel_notifyfd) 
{
  int *pfd;
  ssize_t tx;

  struct iovec iov = {
    .iov_base = &cores_num,
    .iov_len = sizeof(cores_num),
  };

  union {
    char buf[CMSG_SPACE(sizeof(int) * 1)];
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
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 1);

  pfd = (int *) CMSG_DATA(cmsg);
  *pfd = kernel_notifyfd;

  /* send out kernel notify fd */
  if((tx = sendmsg(cfd, &msg, 0)) != sizeof(cores_num)) 
  {
    fprintf(stderr, "uxsocket_send_kernel_notifyfd: tx == %zd\n", tx);
    
    if(tx == -1) 
    {
      fprintf(stderr, "errno == %d %s\n", errno, strerror(errno));
    }
    
    return -1;
  }

  return 0;
}

static int uxsocket_send_core_fds(int cfd, int cores_num) 
{
  uint8_t b = 0;
  int *pfd;
  ssize_t tx;
  int j, n, off;
  struct iovec iov;
  struct msghdr msg;
  struct cmsghdr *cmsg;

  union {
    char buf[CMSG_SPACE(sizeof(int) * 4)];
    struct cmsghdr align;
  } u;

  /* send out fast path fds */
  off = 0;
  for (; off < cores_num;) 
  {
    iov.iov_base = &b;
    iov.iov_len = 1;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);

    n = (cores_num - off >= 4 ? 4 : cores_num - off);
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * n);

    pfd = (int *) CMSG_DATA(cmsg);
    for (j = 0; j < n; j++) 
    {
      pfd[j] = ctxs[off++]->evfd;
    }

    /* send out kernel notify fd */
    if((tx = sendmsg(cfd, &msg, 0)) != sizeof(b)) 
    {
      fprintf(stderr, "uxsocket_send_core_fds: tx fd == %zd\n", tx);
      if(tx == -1) 
      {
        fprintf(stderr, "errno fd == %d - %s\n", errno, strerror(errno));
      }
      abort();
    }
  }

  return 0;
}

int uxsocket_send_shm_fd(int cfd, int shmfd) 
{
  uint8_t b = 0;
  int *pfd;
  ssize_t tx;

  struct iovec iov = {
    .iov_base = &b,
    .iov_len = sizeof(b),
  };

  union {
    char buf[CMSG_SPACE(sizeof(int) * 1)];
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
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 1);

  pfd = (int *) CMSG_DATA(cmsg);
  *pfd = shmfd;

  /* send out kernel notify fd */
  if((tx = sendmsg(cfd, &msg, 0)) != sizeof(b)) 
  {
    fprintf(stderr, "uxsocket_send_shm_fd: tx == %zd\n", tx);
    
    if(tx == -1) 
    {
      fprintf(stderr, "errno == %d %s\n", errno, strerror(errno));
    }
    
    return -1;
  }

  return 0;
}