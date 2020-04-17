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

#ifndef INTERNAL_H_
#define INTERNAL_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <time.h>
#include <poll.h>
#include <netinet/in.h>

#include <tas_ll.h>
#include <utils_sync.h>

enum filehandle_type {
  SOCK_UNUSED = 0,
  SOCK_SOCKET = 1,
  SOCK_CONNECTION = 2,
  SOCK_LISTENER = 3,
};

enum socket_flags {
  SOF_NONBLOCK = 1,
  SOF_BOUND = 2,
  SOF_REUSEPORT = 4,
  SOF_CLOEXEC = 8,
};

enum conn_status {
  SOC_CONNECTING = 0,
  SOC_CONNECTED = 1,
  SOC_FAILED = 2,
  SOC_CLOSED = 3,
};

enum listen_status {
  SOL_OPENING = 0,
  SOL_OPEN = 1,
  SOL_FAILED = 2,
};

enum conn_stflags {
  CSTF_RXCLOSED = 1,
  CSTF_TXCLOSED = 2,
  CSTF_TXCLOSED_ACK = 4,
};

struct socket_pending {
  struct socket *s;
  struct flextcp_context *ctx;
  struct socket_pending *next;
  int fd;
};

struct socket_conn {
  struct flextcp_connection c;
  uint8_t status;
  uint8_t st_flags;
  struct socket *listener;

  void *rx_buf_1;
  void *rx_buf_2;
  size_t rx_len_1;
  size_t rx_len_2;
  struct flextcp_context *ctx;
  int move_status;
};

struct socket_listen {
  struct flextcp_listener l;
  struct socket_pending *pending;
  int backlog;
  uint8_t status;
};

struct socket {
  union {
    struct socket_conn connection;
    struct socket_listen listener;
  } data;
  struct sockaddr_in addr;
  uint8_t flags;
  uint8_t type;
  int refcnt;
  volatile uint32_t sp_lock;

  /** epoll events currently active on this socket */
  uint32_t ep_events;
  /** epoll fds without EPOLLEXCLUSIVE */
  struct epoll_socket *eps;
#if 0
  /** first epoll fd with EPOLLEXCLUSIVE */
  struct epoll_socket *eps_exc_first;
  /** last epoll fd with EPOLLEXCLUSIVE */
  struct epoll_socket *eps_exc_last;
#endif
};

struct epoll {
  /** list of sockets that don't have any unmasked events pending */
  struct epoll_socket *inactive;
  /** list of sockets with unmasked events pending */
  struct epoll_socket *active_first;
  struct epoll_socket *active_last;

  int refcnt;
  volatile uint32_t sp_lock;

  uint32_t num_linux;
  uint32_t num_tas;
  uint32_t num_active;
  uint8_t linux_next;
};

struct epoll_socket {
  struct epoll *ep;
  struct socket *s;

  struct epoll_socket *ep_next;
  struct epoll_socket *ep_prev;

  struct epoll_socket *so_prev;
  struct epoll_socket *so_next;

  epoll_data_t data;
  uint32_t mask;
  uint8_t active;
};

struct sockets_context {
  struct flextcp_context ctx;

  struct pollfd *pollfds_cache;
  size_t pollfds_cache_size;

  struct pollfd *selectfds_cache;
  size_t selectfds_cache_size;
};

int flextcp_fd_init(void);
int flextcp_fd_salloc(struct socket **ps);
int flextcp_fd_ealloc(struct epoll **pe, int fd);
int flextcp_fd_slookup(int fd, struct socket **ps);
int flextcp_fd_elookup(int fd, struct epoll **pe);
void flextcp_fd_srelease(int fd, struct socket *s);
void flextcp_fd_erelease(int fd, struct epoll *ep);
void flextcp_fd_close(int fd);

struct sockets_context *flextcp_sockctx_getfull(void);
struct flextcp_context *flextcp_sockctx_get(void);
int flextcp_sockctx_poll(struct flextcp_context *ctx);
int flextcp_sockctx_poll_n(struct flextcp_context *ctx, unsigned n);

void flextcp_sockclose_finish(struct flextcp_context *ctx, struct socket *s);

void flextcp_epoll_sockinit(struct socket *s);
void flextcp_epoll_sockclose(struct socket *s);
void flextcp_epoll_set(struct socket *s, uint32_t evts);
void flextcp_epoll_clear(struct socket *s, uint32_t evts);
void flextcp_epoll_destroy(struct epoll *ep);

int tas_sock_close(struct socket *sock);
int tas_sock_move(struct socket *s);

int tas_libc_epoll_create1(int flags);
int tas_libc_epoll_ctl(int epfd, int op, int fd,
    struct epoll_event *event);
int tas_libc_epoll_wait(int epfd, struct epoll_event *events,
    int maxevents, int timeout);
int tas_libc_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int tas_libc_close(int fd);
int tas_libc_dup(int oldfd);
int tas_libc_dup2(int oldfd, int newfd);
int tas_libc_dup3(int oldfd, int newfd, int flags);

static inline void socket_lock(struct socket *s)
{
  util_spin_lock(&s->sp_lock);
}

static inline void socket_unlock(struct socket *s)
{
  util_spin_unlock(&s->sp_lock);
}

static inline void epoll_lock(struct epoll *ep)
{
  util_spin_lock(&ep->sp_lock);
}

static inline void epoll_unlock(struct epoll *ep)
{
  util_spin_unlock(&ep->sp_lock);
}

static inline uint64_t get_msecs(void)
{
  int ret;
  struct timespec ts;

  ret = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (ret != 0) {
    perror("flextcp get_msecs: clock_gettime failed\n");
    abort();
  }

  return ts.tv_sec * 1000ULL + (ts.tv_nsec / 1000000ULL);
}


#endif /* ndef INTERNAL_H_ */
