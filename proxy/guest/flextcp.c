#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <assert.h>

#include <tas_memif.h>
#include "internal.h"
#include "ivshmem.h"
#include "../proxy.h"
#include "../../include/kernel_appif.h"

/* Virtualized flextcp that gives the illusion of communicating
   with TAS */

static int vflextcp_uxsocket_init(struct guest_proxy *pxy);
static int vflextcp_virtfd_init(struct guest_proxy *pxy);

static int vflextcp_uxsocket_poll(struct guest_proxy *pxy);
static int vflextcp_uxsocket_accept(struct guest_proxy *pxy);
static int vflextcp_uxsocket_error(struct proxy_application* app);
static int vflextcp_uxsocket_receive(struct guest_proxy *pxy, 
    struct proxy_application* app);
static int vflextcp_uxsocket_handle_msg(struct guest_proxy *pxy,
    struct proxy_application *app);
static int vflextcp_virtfd_poll(struct guest_proxy *pxy);

static int vflextcp_send_kernel_notifyfd(int cfd, int cores_num,
    int kernel_notifyfd);
static int vflextcp_send_core_fds(int cfd, int *core_evfds, int cores_num);
static int vflextcp_send_shm_fd(int cfd, int shmfd);

int vflextcp_init(struct guest_proxy *pxy) 
{
  struct epoll_event ev;
  
  if (vflextcp_uxsocket_init(pxy) != 0) 
  {
    fprintf(stderr, "flextcp_init: "
            "vflextcp_uxsocket_init failed.\n");
    return -1;
  }

  if ((pxy->flextcp_nfd = eventfd(0, EFD_NONBLOCK)) == -1) 
  {
    fprintf(stderr, "flextcp_init: eventfd failed."); 
  }

  if ((pxy->flextcp_epfd = epoll_create1(0)) == -1) 
  {
    fprintf(stderr, "flextcp_init: epoll_create1 failed."); 
    goto error_close_nfd;
  }

  ev.events = EPOLLIN;
  ev.data.ptr = EP_LISTEN;
  if (epoll_ctl(pxy->flextcp_epfd, EPOLL_CTL_ADD, pxy->flextcp_uxfd, &ev) != 0) 
  {
    fprintf(stderr, "flextcp_init: failed to add fd to epfd.");
    goto error_close_epfd;
  }

  if (vflextcp_virtfd_init(pxy) != 0) 
  {
    fprintf(stderr, "flextcp_init: "
            "vflextcp_virtfd_init failed.\n");
  }

  return 0;

error_close_epfd:
  close(pxy->flextcp_epfd);
error_close_nfd:
  close(pxy->flextcp_nfd);

  return -1;
}

int vflextcp_poll(struct guest_proxy *pxy) 
{
  if (vflextcp_uxsocket_poll(pxy) != 0) 
  {
    fprintf(stderr, "flextcp_poll: uxsocket_poll failed.");
    return -1;
  }

  if (vflextcp_virtfd_poll(pxy) != 0) 
  {
    fprintf(stderr, "flextcp_poll: vflextcp_virtfd_poll failed.");
    return -1;
  }

  return 0;
}

static int vflextcp_uxsocket_init(struct guest_proxy *pxy) 
{
  int fd;
  struct sockaddr_un saun;

  if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) 
  {
    fprintf(stderr, "vflextcp_uxsocket_init: socket call failed.\n"); 
    return -1;
  }

  /* create sockaddr */
  memset(&saun, 0, sizeof(saun));
  saun.sun_family = AF_UNIX;
  memcpy(saun.sun_path, KERNEL_SOCKET_PATH, sizeof(KERNEL_SOCKET_PATH));

  /* Unlink deletes socket when program exits */
  unlink(KERNEL_SOCKET_PATH);

  if (bind(fd, (struct sockaddr *) &saun, sizeof(saun))) 
  {
    fprintf(stderr, "vflextcp_uxsocket_init: bind failed.\n");
    goto error_close; 
  }

  if (listen(fd, 5)) 
  {
    fprintf(stderr, "vflextcp_uxsocket_init: listen failed.\n");
    goto error_close; 
  }

  pxy->flextcp_uxfd = fd;

  return 0;

error_close:
  close(fd);

  return -1;
}

int vflextcp_virtfd_init(struct guest_proxy *pxy) {
  int i;
  struct epoll_event ev;
  struct flexnic_info *finfo = pxy->flexnic_info;

  pxy->vepfd = epoll_create1(0);
  if ((pxy->vepfd = epoll_create1(0)) < 0)
  {
    fprintf(stderr,
            "vflextcp_virtfd_init: failed to create fd for vepfd.\n");
    return -1;
  }

  /* allocate memory for virtfds */
  pxy->vfds = malloc(sizeof(int) * finfo->cores_num);
  if (pxy->vfds == NULL) 
  {
    fprintf(stderr, "vflextcp_virtfd_init: failed to allocate memory"
        " for virtual file descriptors.\n");
    goto error_close_vepfd;
  }

  /* initialize virtual file descriptors and add them to epoll */
  for (i = 0; i < finfo->cores_num; i++) 
  {
    if ((pxy->vfds[i] = eventfd(0, EFD_NONBLOCK)) == -1) 
    {
      fprintf(stderr, "vflextcp_virtfd_init: eventfd failed.\n");
      goto error_free;
    }

    ev.events = EPOLLIN;
    ev.data.fd = pxy->vfds[i];
    if (epoll_ctl(pxy->vepfd, EPOLL_CTL_ADD, pxy->vfds[i], &ev) != 0) 
    {
      perror("vflextcp_uxsocket_init: epoll_ctl listen failed.");
      goto error_close;
    }
  }

  return 0;

error_free:
  free(pxy->vfds);
error_close_vepfd:
  close(pxy->vepfd);
error_close:
  close(pxy->flextcp_epfd);
  close(pxy->flextcp_uxfd);

  return -1;
}

static int vflextcp_uxsocket_poll(struct guest_proxy *pxy) 
{
  int n, i;
  struct epoll_event evs[32];
  struct proxy_application *app;

  n = epoll_wait(pxy->epfd, evs, 32, 0);

  for (i = 0; i < n; i++) 
  {
    app = evs[i].data.ptr;

    if (app == EP_LISTEN) 
    {
      vflextcp_uxsocket_accept(pxy); 
    } else if ((evs[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0)
    {
      vflextcp_uxsocket_error(app);
    } else if ((evs[i].events & EPOLLIN) != 0) 
    {
      vflextcp_uxsocket_handle_msg(pxy, app);
    } 
  }
  return 0;
}

static int vflextcp_uxsocket_accept(struct guest_proxy *pxy) 
{
  int cfd, n_cores;
  struct epoll_event ev;
  struct proxy_application *app;

  if ((cfd  = accept(pxy->flextcp_uxfd, NULL, NULL)) < 0) 
  {
    fprintf(stderr, "vflextcp_uxsocket_accept: accept failed\n");
    return -1; 
  }

  n_cores = pxy->flexnic_info->cores_num;
  if (vflextcp_send_kernel_notifyfd(cfd, n_cores, pxy->kernel_notifyfd) != 0) 
  {
    fprintf(stderr, "vflextcp_uxsocket_accept: "
        "failed to send notify_kernelfd.\n");
    return -1;
  }

  if (vflextcp_send_shm_fd(cfd, pxy->dev) != 0) 
  {
    fprintf(stderr, "vflextcp_uxsocket_accept: failed to send shm fd.\n");
    return -1;
  }

  if (vflextcp_send_core_fds(cfd, pxy->vfds, n_cores) != 0) 
  {
    fprintf(stderr, "vflextcp_uxsocket_accept: failed to send core fds.\n");
    return -1;
  }

  /* allocate proxy_application struct */
  if ((app = malloc(sizeof(*app))) == NULL) 
  {
    fprintf(stderr, "vflextcp_uxsocket_accept: "
        "malloc of app struct failed.\n");
    close(cfd);
    return -1;
  }

  /* add to epoll */
  ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
  ev.data.ptr = app;
  if (epoll_ctl(pxy->flextcp_epfd, EPOLL_CTL_ADD, cfd, &ev) != 0) 
  {
    fprintf(stderr, "vflextcp_uxsocket_accept: epoll_ctl failed.");
    free(app);
    close(cfd);
    return -1;
  }

  app->fd = cfd;
  app->req_rx = 0;
  app->closed = 0;
  app->id = pxy->next_app_id;
  app->next = pxy->apps;
  pxy->apps = app;
  pxy->next_app_id++;

  return 0;
}

static int vflextcp_uxsocket_error(struct proxy_application* app) 
{
  close(app->fd);
  app->closed = 1;
  return 0;
}

static int vflextcp_uxsocket_receive(struct guest_proxy *pxy, 
    struct proxy_application* app) 
{
  ssize_t rx;
  int evfd = 0;

  struct iovec iov = {
    .iov_base = &app->proxy_req.req,
    .iov_len = sizeof(app->proxy_req.req) - app->req_rx,
  };

  union {
    char buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr aling;
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

  rx = recvmsg(app->fd, &msg, 0);

  if (msg.msg_controllen > 0) 
  {
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(int)));
    int* data = (int*) CMSG_DATA(cmsg);
    evfd = *data;
  }

  if (rx < 0) 
  {
    perror("vflextcp_uxsocket_receive: recv failed.");
    goto error_abort_app;
  } else if (rx + app->req_rx < sizeof(app->proxy_req.req)) 
  {
    app->req_rx += rx;
    return -1;
  }

  /* request complete */
  app->req_rx = 0;
  app->proxy_req.conn_evfd = evfd;
  app->proxy_req.virt_evfd = pxy->vevfd_next++;
  app->proxy_req.app_id = app->id;
  pxy->context_reqs[app->proxy_req.virt_evfd] = &app->proxy_req;

  return rx;

error_abort_app:
  vflextcp_uxsocket_error(app);
  return -1;
}

static int vflextcp_uxsocket_handle_msg(struct guest_proxy *pxy,
    struct proxy_application *app)
{
  int ret;
  struct context_req_msg msg;
  
  if (vflextcp_uxsocket_receive(pxy, app) > 0) 
  {

    msg.msg_type = MSG_TYPE_CONTEXT_REQ;
    msg.app_id = app->proxy_req.app_id;
    msg.vfd = app->proxy_req.virt_evfd;
    msg.cfd = app->proxy_req.conn_evfd;

    ret = channel_write(pxy->chan, &msg, sizeof(struct context_req_msg)); 
    if (ret < sizeof(struct context_req_msg))
    {
      fprintf(stderr, "vflextcp_uxsocket_handle_msg: "
          "failed to write to channel.\n");
      return -1;
    }
    
    ivshmem_notify_host(pxy);
  }

  return 0;
}

static int vflextcp_virtfd_poll(struct guest_proxy *pxy) {
  int n, i;
  struct epoll_event evs[32];

  n = epoll_wait(pxy->vepfd, evs, 32, 0);
  if (n < 0) 
  {
    perror("vflextcp_virtfd_poll: epoll_wait.");
    return -1;
  }

  for (i = 0; i < n; i++) 
  {
    ivshmem_drain_evfd(evs[i].data.fd);
    fprintf(stderr, "vflextcp_virtfd_poll: "
        "does not expect notify on core fds.\n");
  }
  return 0;
}

int vflextcp_send_kernel_notifyfd(int cfd, int cores_num, int kernel_notifyfd) 
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
    return -1;
  }

  return 0;
}

static int vflextcp_send_core_fds(int cfd, int *core_evfds, int cores_num) 
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
      pfd[j] = core_evfds[off++];
    }

    /* send out kernel notify fd */
    if((tx = sendmsg(cfd, &msg, 0)) != sizeof(b)) 
    {
      fprintf(stderr, "uxsocket_send_core_fds: tx fd == %zd\n", tx);
      return -1;
    }
  }

  return 0;
}

int vflextcp_send_shm_fd(int cfd, int shmfd) 
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
    return -1;
  }

  return 0;
}

int vflextcp_write_context_res(struct guest_proxy *pxy,
    int app_idx, uint8_t *resp_buf, size_t resp_sz) 
{
  ssize_t off, sz;
  struct proxy_application * app;
  
  for (app = pxy->apps; app != NULL; app = app->next) {
    
    if (app->id == app_idx) 
    {
      off = 0;
      while (off < resp_sz) 
      {
        sz = write(app->fd, resp_buf, resp_sz - off);
        
        if (sz < 0) {
          perror("flextcp_uxsocket_error: write failed.");
          return -1;
        }
        
        off += sz; 
      }
     
      break;
    }
  }

  return 0;
}

int vflextcp_poke(struct guest_proxy *pxy, int virt_fd) 
{
  int evfd;
  uint64_t w = 1;
  assert(virt_fd < MAX_CONTEXT_REQ);

  evfd = pxy->context_reqs[virt_fd]->conn_evfd;
  if (write(evfd, &w, sizeof(uint64_t)) != sizeof(uint64_t)) 
  {
    fprintf(stderr, "vflextcp_poke: write failed.\n");
    return -1;
  }

  return 0;
}