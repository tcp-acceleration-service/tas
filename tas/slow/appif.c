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

/**
 * @brief Application-Kernel interface.
 * @file appif.c
 *
 * The application-kernel interface consists of two parts: per-application unix
 * socket for setup, and per-core application context queues for normal
 * operation. The message format for both those is defined in kernel_appif.h.
 *
 * During application initialization the application opens a Unix stream socket
 * to the kernel. This stream socket is then used to negotiate creation of one
 * or more application context queues in the shared packet memory region.
 *
 * Communication on the application context queues is handled in appif_ctx.c.
 *
 * The unix socket is handled on a separate thread so a blocking epoll can be
 * used. To avoid synchronization in other kernel parts the ux socket thread
 * just communicates on the sockets and uses two queues #ux_to_poll and
 * #poll_to_ux to communicate with the main thread. The main thread then calls
 * into other modules to register the context with flexnic etc.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <tas.h>
#include "internal.h"
#include "appif.h"
#include <kernel_appif.h>
#include <utils_nbqueue.h>
#include <tas_memif.h>
#include <fastpath.h>

/** epoll data for listening socket */
#define EP_LISTEN (NULL)
/** epoll data for notifyfd associated with #poll_to_ux queue */
#define EP_NOTIFY ((void *) (-1))

static int uxsocket_init(void);
static void *uxsocket_thread(void *arg);
static void uxsocket_accept(void);
static void uxsocket_notify(void);
static void uxsocket_error(struct application *app);
static void uxsocket_receive(struct application *app);
static void uxsocket_notify_app(struct application *app);

/** Listening UX socket for applications to connect to */
static int uxfd = -1;
/** Epoll object used by UX socket thread */
static int epfd = -1;
/** eventfd for notifying UX thread about completion on poll_to_ux */
static int notifyfd = -1;
/** Completions of asynchronous NIC operations for UX socket thread */
static struct nbqueue poll_to_ux;
/** Queue to pass structs for new applications from UX to poll thread */
static struct nbqueue ux_to_poll;

/** Pthread handle for UX socket thread */
static pthread_t pt_ux;

/** Freelist for NIC doorbells to be allocated to applications. */
static struct app_doorbell *free_doorbells = NULL;
/** Next unused application id, used for allocation */
static uint16_t app_id_next = 0;

/** Linked list of all application structs */
static struct application *applications = NULL;


int appif_init(void)
{
  struct app_doorbell *adb;
  uint32_t i;

  if (uxsocket_init()) {
    return -1;
  }

  /* create freelist of doorbells (0 is used by kernel) */
  for (i = FLEXNIC_PL_APPST_CTX_NUM; i > 0; i--) {
    if ((adb = malloc(sizeof(*adb))) == NULL) {
      perror("appif_init: malloc doorbell failed");
      return -1;
    }
    adb->id = i;
    adb->next = free_doorbells;
    free_doorbells = adb;
  }

  nbqueue_init(&ux_to_poll);
  nbqueue_init(&poll_to_ux);

  if (pthread_create(&pt_ux, NULL, uxsocket_thread, NULL) != 0) {
    return -1;
  }

  return 0;
}

unsigned appif_poll(void)
{
  uint8_t *p;
  struct application *app;
  struct app_context *ctx;
  ssize_t ret;
  uint16_t i;
  uint64_t rxq_offs[tas_info->cores_num], txq_offs[tas_info->cores_num];
  uint64_t cnt = 1;
  unsigned n = 0;

  /* add new applications to list */
  while ((p = nbqueue_deq(&ux_to_poll)) != NULL) {
    app = (struct application *) (p - offsetof(struct application, nqe));
    app->next = applications;
    applications = app;
  }

  for (app = applications; app != NULL; app = app->next) {
    /* register context with NIC */
    if (app->need_reg_ctx != NULL) {
      ctx = app->need_reg_ctx;
      app->need_reg_ctx = NULL;

      for (i = 0; i < tas_info->cores_num; i++) {
        rxq_offs[i] = app->resp->flexnic_qs[i].rxq_off;
        txq_offs[i] = app->resp->flexnic_qs[i].txq_off;
      }

      if (nicif_appctx_add(app->id, ctx->doorbell->id, rxq_offs,
            app->req.rxq_len, txq_offs, app->req.txq_len, ctx->evfd) != 0)
      {
        fprintf(stderr, "appif_poll: registering context failed\n");
        uxsocket_error(app);
        continue;
      }

      nbqueue_enq(&poll_to_ux, &app->comp.el);
      ret = write(notifyfd, &cnt, sizeof(cnt));
      if (ret <= 0) {
        perror("appif_poll: error writing to notify fd");
      }
    }

    for (ctx = app->contexts; ctx != NULL; ctx = ctx->next) {
      if (ctx->ready == 0) {
        continue;
      }
      n += appif_ctx_poll(app, ctx);
    }
  }

  return n;
}


static int uxsocket_init(void)
{
  int fd, efd, nfd;
  struct sockaddr_un saun;
  struct epoll_event ev;

  if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    perror("uxsocket_init: socket failed");
    goto error_exit;
  }

  memset(&saun, 0, sizeof(saun));
  saun.sun_family = AF_UNIX;
  memcpy(saun.sun_path, KERNEL_SOCKET_PATH, sizeof(KERNEL_SOCKET_PATH));
  if (bind(fd, (struct sockaddr *) &saun, sizeof(saun))) {
    perror("uxsocket_init: bind failed");
    goto error_close;
  }

  if (listen(fd, 5)) {
    perror("uxsocket_init: listen failed");
    goto error_close;
  }

  if ((nfd = eventfd(0, EFD_NONBLOCK)) == -1) {
    perror("uxsocket_init: eventfd failed");
  }

  if ((efd = epoll_create1(0)) == -1) {
    perror("uxsocket_init: epoll_create1 failed");
    goto error_close;
  }

  ev.events = EPOLLIN;
  ev.data.ptr = EP_LISTEN;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) != 0) {
    perror("uxsocket_init: epoll_ctl listen failed");
    goto error_close_ep;
  }

  ev.events = EPOLLIN;
  ev.data.ptr = EP_NOTIFY;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, nfd, &ev) != 0) {
    perror("uxsocket_init: epoll_ctl notify failed");
    goto error_close_ep;
  }

  uxfd = fd;
  epfd = efd;
  notifyfd = nfd;
  return 0;

error_close_ep:
  close(efd);
error_close:
  close(fd);
error_exit:
  return -1;
}

static void *uxsocket_thread(void *arg)
{
  int n, i;
  struct epoll_event evs[32];
  struct application *app;

  while (1) {
  again:
    n = epoll_wait(epfd, evs, 32, -1);
    if (n < 0) {
      if(errno == EINTR) {
	// XXX: To support attaching GDB
	goto again;
      }
      perror("uxsocket_thread: epoll_wait");
      abort();
    }

    for (i = 0; i < n; i++) {
      app = evs[i].data.ptr;
      if (app == EP_LISTEN) {
        uxsocket_accept();
      } else if (app == EP_NOTIFY) {
        uxsocket_notify();
      } else {
        if ((evs[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0) {
          uxsocket_error(app);
        } else if ((evs[i].events & EPOLLIN) != 0) {
          uxsocket_receive(app);
        }
      }
    }

    /* signal main slowpath thread */
    notify_slowpath_core();
  }

  return NULL;
}

static void uxsocket_accept(void)
{
  int cfd, *pfd;
  struct application *app;
  struct epoll_event ev;
  size_t sz;
  ssize_t tx;
  uint32_t off, j, n;
  uint8_t b = 0;

  /* new connection on unix socket */
  if ((cfd = accept(uxfd, NULL, NULL)) < 0) {
    fprintf(stderr, "uxsocket_accept: accept failed\n");
    return;
  }

  struct iovec iov = {
    .iov_base = &tas_info->cores_num,
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

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));

  pfd = (int *) CMSG_DATA(cmsg);
  *pfd = kernel_notifyfd;

  /* send out kernel notify fd */
  if((tx = sendmsg(cfd, &msg, 0)) != sizeof(uint32_t)) {
    fprintf(stderr, "tx == %zd\n", tx);
    if(tx == -1) {
      fprintf(stderr, "errno == %d\n", errno);
    }
  }

  /* send out fast path fds */
  off = 0;
  for (; off < tas_info->cores_num;) {
    iov.iov_base = &b;
    iov.iov_len = 1;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);

    n = (tas_info->cores_num - off >= 4 ? 4 : tas_info->cores_num - off);

    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * n);

    pfd = (int *) CMSG_DATA(cmsg);
    for (j = 0; j < n; j++) {
      pfd[j] = ctxs[off++]->evfd;
    }

    /* send out kernel notify fd */
    if((tx = sendmsg(cfd, &msg, 0)) != 1) {
      fprintf(stderr, "tx fd == %zd\n", tx);
      if(tx == -1) {
        fprintf(stderr, "errno fd == %d\n", errno);
      }
      abort();
    }
  }

  /* allocate application struct */
  if ((app = malloc(sizeof(*app))) == NULL) {
    fprintf(stderr, "uxsocket_accept: malloc of app struct failed\n");
    close(cfd);
    return;
  }

  sz = sizeof(*app->resp) +
    tas_info->cores_num * sizeof(app->resp->flexnic_qs[0]);
  app->resp_sz = sz;
  if ((app->resp = malloc(sz)) == NULL) {
    fprintf(stderr, "uxsocket_accept: malloc of app resp struct failed\n");
    free(app);
    close(cfd);
    return;
  }

  /* add to epoll */
  ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
  ev.data.ptr = app;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) != 0) {
    perror("uxsocket_accept: epoll_ctl failed");
    free(app->resp);
    free(app);
    close(cfd);
    return;
  }

  app->fd = cfd;
  app->contexts = NULL;
  app->need_reg_ctx = NULL;
  app->closed = false;
  app->conns = NULL;
  app->listeners = NULL;
  app->id = app_id_next++;
  nbqueue_enq(&ux_to_poll, &app->nqe);
}

static void uxsocket_notify(void)
{
  uint8_t *p;
  struct application *app;
  uint64_t x;
  ssize_t ret;

  ret = read(notifyfd, &x, sizeof(x));
  if (ret == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    perror("uxsocket_notify: read on notifyfd failed");
    abort();
  }

  while ((p = nbqueue_deq(&poll_to_ux)) != NULL) {
    app = (struct application *) (p - offsetof(struct application, comp.el));
    uxsocket_notify_app(app);
  }
}

static void uxsocket_error(struct application *app)
{
  close(app->fd);
  app->closed = true;
}

static void uxsocket_receive(struct application *app)
{
  ssize_t rx;
  struct app_context *ctx;
  struct packetmem_handle *pm_in, *pm_out;
  uintptr_t off_in, off_out, off_rxq, off_txq;
  size_t kin_qsize, kout_qsize, ctx_sz;
  struct epoll_event ev;
  uint16_t i;
  int evfd = 0;

  /* receive data to hopefully complete request */
  struct iovec iov = {
    .iov_base = &app->req,
    .iov_len = sizeof(app->req) - app->req_rx,
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
  rx = recvmsg(app->fd, &msg, 0);

  if(msg.msg_controllen > 0) {
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    assert(cmsg->cmsg_len == CMSG_LEN(sizeof(int)));
    int* data = (int*) CMSG_DATA(cmsg);
    evfd = *data;
  }

  if (rx < 0) {
    perror("uxsocket_receive: recv failed");
    goto error_abort_app;
  } else if (rx + app->req_rx < sizeof(app->req)) {
    /* request not complete yet */
    app->req_rx += rx;
    return;
  }

  /* request complete */
  app->req_rx = 0;

  /* allocate context struct */
  ctx_sz = sizeof(*ctx) + tas_info->cores_num * sizeof(ctx->handles[0]);
  if ((ctx = malloc(ctx_sz)) == NULL) {
    perror("uxsocket_receive: ctx malloc failed");
    goto error_ctxmalloc;
  }

  /* queue sizes */
  kin_qsize = config.app_kin_len;
  kout_qsize = config.app_kout_len;

  /* allocate packet memory for kernel queues */
  if (packetmem_alloc(kin_qsize, &off_in, &pm_in) != 0) {
    fprintf(stderr, "uxsocket_receive: packetmem_alloc in failed\n");
    goto error_pktmem_in;
  }
  if (packetmem_alloc(kout_qsize, &off_out, &pm_out) != 0) {
    fprintf(stderr, "uxsocket_receive: packetmem_alloc out failed\n");
    goto error_pktmem_out;
  }

  /* allocate packet memory for flexnic queues */
  for (i = 0; i < tas_info->cores_num; i++) {
    if (packetmem_alloc(app->req.rxq_len, &off_rxq, &ctx->handles[i].rxq)
        != 0)
    {
      fprintf(stderr, "uxsocket_receive: packetmem_alloc rxq failed\n");
      goto error_pktmem;
    }
    if (packetmem_alloc(app->req.txq_len, &off_txq, &ctx->handles[i].txq)
        != 0)
    {
      fprintf(stderr, "uxsocket_receive: packetmem_alloc txq failed\n");
      packetmem_free(ctx->handles[i].rxq);
      goto error_pktmem;
    }
    memset((uint8_t *) tas_shm + off_rxq, 0, app->req.rxq_len);
    memset((uint8_t *) tas_shm + off_txq, 0, app->req.txq_len);
    app->resp->flexnic_qs[i].rxq_off = off_rxq;
    app->resp->flexnic_qs[i].txq_off = off_txq;
  }

  /* allocate doorbell */
  if ((ctx->doorbell = free_doorbells) == NULL) {
    fprintf(stderr, "uxsocket_receive: allocating doorbell failed\n");
    goto error_dballoc;
  }
  free_doorbells = ctx->doorbell->next;


  /* initialize queuepair struct and queues */
  ctx->app = app;

  ctx->kin_handle = pm_in;
  ctx->kin_base = (uint8_t *) tas_shm + off_in;
  ctx->kin_len = kin_qsize / sizeof(struct kernel_appout);
  ctx->kin_pos = 0;
  memset(ctx->kin_base, 0, kin_qsize);

  ctx->kout_handle = pm_out;
  ctx->kout_base = (uint8_t *) tas_shm + off_out;
  ctx->kout_len = kout_qsize / sizeof(struct kernel_appin);
  ctx->kout_pos = 0;
  memset(ctx->kout_base, 0, kout_qsize);

  ctx->ready = 0;
  assert(evfd != 0);	// XXX: Will be 0 if request was broken up
  ctx->evfd = evfd;

  ctx->next = app->contexts;
  MEM_BARRIER();
  app->contexts = ctx;

  /* initialize response */
  app->resp->app_out_off = off_in;
  app->resp->app_out_len = kin_qsize;
  app->resp->app_in_off = off_out;
  app->resp->app_in_len = kout_qsize;
  app->resp->flexnic_db_id = ctx->doorbell->id;
  app->resp->flexnic_qs_num = tas_info->cores_num;
  app->resp->status = 0;

  /* no longer wait on epoll in for this socket until we get the completion */
  ev.events = EPOLLRDHUP | EPOLLERR;
  ev.data.ptr = app;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, app->fd, &ev) != 0) {
    /* not sure how to  handle this */
    perror("uxsocket_receive: epoll_ctl failed");
    abort();
  }

  app->need_reg_ctx_done = ctx;
  MEM_BARRIER();
  app->need_reg_ctx = ctx;
#if 0
  /* send out response */
  tx = send(app->fd, &resp, sizeof(resp), 0);
  if (tx < 0) {
    perror("uxsocket_receive: send failed");
    goto error_abort_app;
  } else if (tx < sizeof(resp)) {
    /* FIXME */
    fprintf(stderr, "uxsocket_receive: short send for response (TODO)\n");
    goto error_abort_app;
  }
#endif

  return;


error_dballoc:
  /* TODO: for () packetmem_free(ctx->txq_handle) */
error_pktmem:
  packetmem_free(pm_out);
error_pktmem_out:
  packetmem_free(pm_in);
error_pktmem_in:
  free(ctx);
error_ctxmalloc:
error_abort_app:
  uxsocket_error(app);
  return;

}

static void uxsocket_notify_app(struct application *app)
{
  ssize_t tx;
  struct epoll_event ev;
  struct app_context *ctx;

  if (app->comp.status != 0) {
    /* TODO: cleanup and return error */
    fprintf(stderr, "uxsocket_notify_app: status = %d, terminating app\n",
        app->comp.status);
    goto error_status;
    return;
  }

  ctx = app->need_reg_ctx_done;
  ctx->ready = 1;

  /* send out response */
  tx = send(app->fd, app->resp, app->resp_sz, 0);
  if (tx < 0) {
    perror("uxsocket_notify_app: send failed");
    goto error_send;
  } else if (tx < app->resp_sz) {
    /* FIXME */
    fprintf(stderr, "uxsocket_notify_app: short send for response (TODO)\n");
    goto error_send;
  }

  /* wait for epoll in again */
  ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
  ev.data.ptr = app;
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, app->fd, &ev) != 0) {
    /* not sure how to  handle this */
    perror("uxsocket_notify_app: epoll_ctl failed");
    abort();
  }

  return;

error_status:
error_send:
    uxsocket_error(app);
}
