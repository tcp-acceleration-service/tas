  #include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <assert.h>

#include <tas_memif.h>
#include <utils.h>
#include <utils_shm.h>
#include "internal.h"
#include "flextcp.h"
#include "ivshmem.h"
#include "../proxy.h"
#include "../../include/kernel_appif.h"

/* Virtualized flextcp that gives the illusion of communicating
   with TAS */

static int vflextcp_uxsocket_init(struct guest_proxy *pxy);

static int vflextcp_uxsocket_poll(struct guest_proxy *pxy);
static int vflextcp_uxsocket_accept(struct guest_proxy *pxy);
static int vflextcp_uxsocket_error(struct proxy_application* app);
static int vflextcp_uxsocket_receive(struct guest_proxy *pxy, 
    struct proxy_application* app, int *actx_id);
static int vflextcp_uxsocket_handle_msg(struct guest_proxy *pxy,
    struct proxy_application *app);

static int vflextcp_tas_poke_poll(struct guest_proxy *pxy);
static int vflextcp_handle_tas_kernel_poke(struct guest_proxy *pxy, 
    struct poke_tas_kernel_msg *msg);
static int vflextcp_handle_tas_core_poke(struct guest_proxy *pxy, 
    struct poke_tas_core_msg *msg);

static int vflextcp_send_kernel_notifyfd(int cfd, int cores_num,
    int kernel_notifyfd);
static int vflextcp_send_core_fds(int cfd, int *core_evfds, int cores_num);
static int vflextcp_send_shm_fd(int cfd, int shmfd);

int vflextcp_init(struct guest_proxy *pxy) 
{
  struct epoll_event ev;
  
  if (vflextcp_uxsocket_init(pxy) != 0) 
  {
    fprintf(stderr, "vflextcp_init: "
            "vflextcp_uxsocket_init failed.\n");
    return -1;
  }

  if ((pxy->flextcp_nfd = eventfd(0, EFD_NONBLOCK)) == -1) 
  {
    fprintf(stderr, "vflextcp_init: eventfd failed."); 
  }

  if ((pxy->flextcp_epfd = epoll_create1(0)) == -1) 
  {
    fprintf(stderr, "vflextcp_init: epoll_create1 failed."); 
    goto error_close_nfd;
  }

  if ((pxy->epfd = epoll_create1(0)) < 0)
  {
    fprintf(stderr,
        "vflextcp_init: failed to create fd for vepfd.\n");
    goto error_close_epfd;
    return -1;
  }

  ev.events = EPOLLIN;
  ev.data.ptr = EP_LISTEN;
  if (epoll_ctl(pxy->flextcp_epfd, EPOLL_CTL_ADD, pxy->flextcp_uxfd, &ev) != 0) 
  {
    fprintf(stderr, "vflextcp_init: failed to add fd to epfd.");
    goto error_close_vepfd;
  }

  if (epoll_ctl(pxy->block_epfd, EPOLL_CTL_ADD, pxy->flextcp_uxfd, &ev) != 0) 
  {
    fprintf(stderr, "vflextcp_init: failed to add fd to block_epfd.");
    goto error_close_vepfd;
  }


  return 0;

error_close_vepfd:
  close(pxy->epfd);
error_close_epfd:
  close(pxy->flextcp_epfd);
error_close_nfd:
  close(pxy->flextcp_nfd);

  return -1;
}

static int vflextcp_uxsocket_init(struct guest_proxy *pxy) 
{
  int fd, groupid;
  struct sockaddr_un saun;

  if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) 
  {
    fprintf(stderr, "vflextcp_uxsocket_init: socket call failed.\n"); 
    return -1;
  }

  /* Use groupid 0 as default */
  groupid = 0;
  /* create sockaddr */
  memset(&saun, 0, sizeof(saun));
  saun.sun_family = AF_UNIX;
  snprintf(saun.sun_path, sizeof(saun.sun_path), 
      "%s_vm_%d", KERNEL_SOCKET_PATH, groupid);

  unlink(saun.sun_path);
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

int vflextcp_kernel_notifyfd_init(struct guest_proxy *pxy)
{
  struct epoll_event ev;
  struct poke_event *poke_ev;
  struct poke_tas_kernel_msg *msg;

  if ((pxy->kernel_notifyfd = eventfd(0, EFD_NONBLOCK)) < 0)
  {
    fprintf(stderr, "vflextcp_kernel_notifyfd_init: "
        "failed to create kernel_notifyfd.\n");
    return -1;
  }

  ev.events = EPOLLIN;

  if ((poke_ev = malloc(sizeof(struct poke_event))) == NULL)
  {
    fprintf(stderr, "vflextcp_kernel_notifyfd_init: "
      "failed to allocate poke_event.\n");
    return -1;
  }

  if ((msg = malloc(sizeof(struct poke_tas_kernel_msg))) == NULL)
  {
    fprintf(stderr, "vflextcp_kernel_notifyfd_init: "
        "failed to alllocate msg.\n");
    return -1;
  }

  msg->msg_type = MSG_TYPE_POKE_TAS_KERNEL;
  
  /* This is redundant for a kernel msg but we need to
     know the message type beforehand in the poke_event
     and for other poke msgs you need to pass the core id */
  poke_ev->msg_type = MSG_TYPE_POKE_TAS_KERNEL;
  poke_ev->msg = msg;
  ev.data.ptr = poke_ev;

  if (epoll_ctl(pxy->epfd, EPOLL_CTL_ADD, pxy->kernel_notifyfd, &ev) != 0) 
  {
    perror("vflextcp_kernel_notifyfd_init: epoll_ctl listen failed.");
    return -1;
  }

  if (epoll_ctl(pxy->block_epfd, EPOLL_CTL_ADD,
      pxy->kernel_notifyfd, &ev) != 0) 
  {
    perror("vflextcp_kernel_notifyfd_init: "
        "epoll_ctl listen  for block_epfd failed.");
    return -1;
  }

  return 0;
}

int vflextcp_core_evfds_init(struct guest_proxy *pxy) {
  int i;
  struct epoll_event ev;
  struct poke_event *poke_ev;
  struct poke_tas_core_msg *msg;
  struct flexnic_info *finfo = pxy->flexnic_info;

  /* allocate memory for virtfds */
  pxy->core_evfds = malloc(sizeof(int) * finfo->cores_num);
  if (pxy->core_evfds == NULL) 
  {
    fprintf(stderr, "vflextcp_core_evfds_init: failed to allocate memory"
        " for virtual file descriptors.\n");
    goto error_close_vepfd;
  }

  /* initialize fast path evfds and add them to epoll */
  for (i = 0; i < finfo->cores_num; i++) 
  {
    if ((pxy->core_evfds[i] = eventfd(0, EFD_NONBLOCK)) == -1) 
    {
      fprintf(stderr, "vflextcp_core_evfds_init: eventfd failed.\n");
      goto error_free;
    }

    ev.events = EPOLLIN;

    if ((poke_ev = malloc(sizeof(struct poke_event))) == NULL)
    {
      fprintf(stderr, "vflextcp_core_evfds_init: "
          "failed to allocate poke_event.\n");
      return -1;
    }

    if ((msg = malloc(sizeof(struct poke_tas_core_msg))) == NULL)
    {
      fprintf(stderr, "vflextcp_core_evfds_init: failed to allocate poke msg\n");
    }

    msg->msg_type = MSG_TYPE_POKE_TAS_CORE;
    msg->core_id = i;

  /* This is redundant for a kernel msg but we need to
     know the message type beforehand in the poke_event
     and for other poke msgs you need to pass the core id */
    poke_ev->msg_type = MSG_TYPE_POKE_TAS_CORE;
    poke_ev->msg = msg;
    ev.data.ptr = poke_ev;

    if (epoll_ctl(pxy->epfd, EPOLL_CTL_ADD, pxy->core_evfds[i], &ev) != 0) 
    {
      perror("vflextcp_core_evfds_init: epoll_ctl listen failed.");
      goto error_close;
    }

    if (epoll_ctl(pxy->block_epfd, EPOLL_CTL_ADD,
        pxy->core_evfds[i], &ev) != 0) 
    {
      perror("vflextcp_core_evfds_init: "
          "epoll_ctl listen for block_epfd failed.");
      goto error_close;
    }
  }

  return 0;

error_free:
  free(pxy->core_evfds);
error_close_vepfd:
  close(pxy->epfd);
error_close:
  close(pxy->flextcp_epfd);
  close(pxy->flextcp_uxfd);

  return -1;
}

int vflextcp_serve_tasinfo(uint8_t *info, ssize_t size)
{
  struct flexnic_info *pxy_tas_info = NULL;
  umask(0);

  /* create shm for tas_info */
  pxy_tas_info = util_create_shmsiszed(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES,
      NULL, NULL);

  if (pxy_tas_info == NULL)
  {
    fprintf(stderr, "vflextcp_serve_tasinfo: "
        "mapping tas_info in proxy failed.\n");
    util_destroy_shm(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES, pxy_tas_info);
    return -1;
  }

  memcpy(pxy_tas_info, info, size);

  return 0;
}

int vflextcp_poll(struct guest_proxy *pxy) 
{
  int ret, n = 0; 
  if ((ret = vflextcp_uxsocket_poll(pxy)) < 0) 
  {
    fprintf(stderr, "flextcp_poll: uxsocket_poll failed.\n");
    return -1;
  }
  n += ret;

  if ((ret = vflextcp_tas_poke_poll(pxy)) < 0) 
  {
    fprintf(stderr, "flextcp_poll: vflextcp_virtfd_poll failed.\n");
    return -1;
  }
  n += ret;

  return n;
}

/*****************************************************************************/
/* Uxsocket */

static int vflextcp_uxsocket_poll(struct guest_proxy *pxy) 
{
  int n, i;
  struct epoll_event evs[32];
  struct proxy_application *app;

  n = epoll_wait(pxy->flextcp_epfd, evs, 32, 0);
  
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
  
  return n;
}

static int vflextcp_uxsocket_accept(struct guest_proxy *pxy) 
{
  int cfd, ret;
  struct newapp_req_msg req_msg;

  if ((cfd  = accept(pxy->flextcp_uxfd, NULL, NULL)) < 0) 
  {
    fprintf(stderr, "vflextcp_uxsocket_accept: accept failed\n");
    return -1; 
  }

  /* Register app with host */
  req_msg.msg_type = MSG_TYPE_NEWAPP_REQ;
  req_msg.cfd = cfd;
  ret = channel_write(pxy->chan, &req_msg, sizeof(req_msg));
  if (ret != sizeof(req_msg))
  {
    fprintf(stderr, "vflextcp_uxsocket_accept: "
        "failed to write req_msg to chan.\n");
    return -1;
  }
  /* Notify host */
  ivshmem_notify_host(pxy);

  return 0;
}

int vflextcp_handle_newapp_res(struct guest_proxy *pxy, 
    struct newapp_res_msg *msg)
{
  int n_cores, cfd;
  struct epoll_event ev;
  struct proxy_application *app;

  n_cores = pxy->flexnic_info->cores_num;
  cfd = msg->cfd;
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

  if (vflextcp_send_core_fds(cfd, pxy->core_evfds, n_cores) != 0) 
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

  if (epoll_ctl(pxy->block_epfd, EPOLL_CTL_ADD, cfd, &ev) != 0) 
  {
    fprintf(stderr, "vflextcp_uxsocket_accept: epoll_ctl for block epfd failed.");
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
    struct proxy_application* app, int *actx_id) 
{
  ssize_t rx;
  int ctx_evfd = 0;
  struct proxy_context_req *ctx_req;

  ctx_req = malloc(sizeof(struct proxy_context_req));
  if (ctx_req == NULL)
  {
    fprintf(stderr, "vflextcp_uxsocket_receive: "
        "failed to malloc context req.\n");
    goto error_abort_app;
  }

  struct iovec iov = {
    .iov_base = &ctx_req->req,
    .iov_len = sizeof(ctx_req->req) - app->req_rx,
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
    ctx_evfd = *data;
  }

  if (rx < 0) 
  {
    perror("vflextcp_uxsocket_receive: recv failed.");
    goto error_abort_app;
  } else if (rx + app->req_rx < sizeof(ctx_req->req)) 
  {
    app->req_rx += rx;
    return -1;
  }

  /* request complete */
  app->req_rx = 0;
  ctx_req->actx_evfd = ctx_evfd;
  ctx_req->ctxreq_id = pxy->ctxreq_id_next++;
  ctx_req->app_id = app->id;
  pxy->context_reqs[ctx_req->ctxreq_id] = ctx_req;

  *actx_id = ctx_req->ctxreq_id;

  return rx;

error_abort_app:
  vflextcp_uxsocket_error(app);
  return -1;
}

static int vflextcp_uxsocket_handle_msg(struct guest_proxy *pxy,
    struct proxy_application *app)
{
  int ret, actx_id;
  struct context_req_msg msg;
  struct proxy_context_req *ctx_req;

  if (vflextcp_uxsocket_receive(pxy, app, &actx_id) > 0) 
  {
    
    ctx_req = pxy->context_reqs[actx_id];

    msg.msg_type = MSG_TYPE_CONTEXT_REQ;
    msg.app_id = app->id;
    msg.ctxreq_id = ctx_req->ctxreq_id;
    msg.actx_evfd = ctx_req->actx_evfd;

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

/*****************************************************************************/
/* Poke TAS */

static int vflextcp_tas_poke_poll(struct guest_proxy *pxy) {
  int n, i;
  struct poke_event *poke_ev;
  struct epoll_event evs[2];

  n = epoll_wait(pxy->epfd, evs, 2, 0);
  if (n < 0) 
  {
    perror("vflextcp_tas_poke_poll: epoll_wait");
    return -1;
  }

  for (i = 0; i < n; i++) 
  {
    if (evs[i].events & EPOLLIN)
    {
      poke_ev = evs[i].data.ptr;

      if (poke_ev->msg_type == MSG_TYPE_POKE_TAS_KERNEL)
      {
        vflextcp_handle_tas_kernel_poke(pxy, poke_ev->msg);
      } else if (poke_ev->msg_type == MSG_TYPE_POKE_TAS_CORE)
      {
        vflextcp_handle_tas_core_poke(pxy, poke_ev->msg);
      }
    }
  }

  return n;
}

static int vflextcp_handle_tas_kernel_poke(struct guest_proxy *pxy, 
    struct poke_tas_kernel_msg *msg)
{
  int ret;

  ivshmem_drain_evfd(pxy->kernel_notifyfd);
  ret = channel_write(pxy->chan, msg, sizeof(struct poke_tas_kernel_msg));

  if (ret != sizeof(struct poke_tas_kernel_msg))
  {
    fprintf(stderr, "vflextcp_handle_tas_kernel_poke: "
        "failed to write kernel poke msg.\n");
    return -1;
  }

  ivshmem_notify_host(pxy);
  return 0;
}

static int vflextcp_handle_tas_core_poke(struct guest_proxy *pxy, 
    struct poke_tas_core_msg *msg)
{
  int ret;

  ivshmem_drain_evfd(pxy->core_evfds[msg->core_id]);
  ret = channel_write(pxy->chan, msg, sizeof(struct poke_tas_core_msg));

  if (ret != sizeof(struct poke_tas_core_msg))
  {
    fprintf(stderr, "vflextcp_handle_tas_core_poke: " 
        "failed to write core poke msg.\n");
    return -1;
  }

  ivshmem_notify_host(pxy);
  return 0;
}

/*****************************************************************************/
/* Others */

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

static int  vflextcp_send_core_fds(int cfd, int *core_evfds, int cores_num) 
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

int vflextcp_poke(struct guest_proxy *pxy, int actx_id) 
{
  int evfd;
  uint64_t w = 1;
  assert(actx_id < MAX_CONTEXT_REQ);

  evfd = pxy->context_reqs[actx_id]->actx_evfd;
  if (write(evfd, &w, sizeof(uint64_t)) != sizeof(uint64_t)) 
  {
    fprintf(stderr, "vflextcp_poke: write failed.\n");
    return -1;
  }

  return 0;
}
