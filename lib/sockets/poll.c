/*
 * Copyright 2020 University of Washington, Max Planck Institute for
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

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <poll.h>

#include <tas_sockets.h>
#include "internal.h"

static inline uint32_t events_epoll2poll(uint32_t epoll_event);
static int pollfd_cache_alloc(struct sockets_context *ctx, size_t n);
static int pollfd_cache_prepare(struct sockets_context *ctx, struct pollfd *fds,
    nfds_t nfds, nfds_t num_linuxfds);


int tas_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout)
{
  assert(!"NYI");
  errno = ENOTSUP;
  return -1;
}

int tas_pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    const struct timespec *timeout, const sigset_t *sigmask)
{
  assert(!"NYI");
  errno = ENOTSUP;
  return -1;
}

int tas_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
  struct sockets_context *ctx;
  struct socket *s;
  struct pollfd *p;
  nfds_t nfds_linux, nfds_tas, i;
  uint32_t s_events;
  int ret, active_fds = 0, mixed_events = 0, j;
  uint64_t mtimeout = 0;
  int first = 1;

  ctx = flextcp_sockctx_getfull();

  if (timeout != 0 && timeout != -1)
    mtimeout = get_msecs() + timeout;

  do {
    flextcp_sockctx_poll_n(&ctx->ctx, nfds);

    nfds_linux = nfds_tas = 0;
    /* first process any tas fds */
    for (i = 0; i < nfds; i++) {
      p = &fds[i];

      if (flextcp_fd_slookup(p->fd, &s) != 0) {
        /* this is a linux fd */
        nfds_linux++;
        continue;
      }
      nfds_tas++;

      s_events = s->ep_events;
      flextcp_fd_srelease(p->fd, s);

      if ((p->events & ~(POLLIN | POLLPRI | POLLOUT | POLLRDHUP | POLLERR |
              POLLHUP | POLLRDNORM | POLLWRNORM | POLLNVAL)) != 0)
      {
        errno = EINVAL;
        fprintf(stderr, "tas_pselect: unsupported fd flags (%x)\n", p->events);
        return -1;
      }

      p->revents = p->events & events_epoll2poll(s_events);
      if (p->revents != 0)
        active_fds++;
    }

    /* now look at linux fds */
    if (nfds_linux > 0 && nfds_tas == 0) {
      /* only linux, this is the easy case -> just call into linux */
      return tas_libc_poll(fds, nfds, timeout);
    } else if (nfds_linux > 0) {
      /* mixed tas and linux, this is annoying as we need to separate out linux
       * fds */

      /* the first iteration, we first need to copy only the linux fds into the
       * pollfd_cache */
      if (first) {
        if (pollfd_cache_prepare(ctx, fds, nfds, nfds_linux) != 0) {
          errno = ENOMEM;
          return -1;
        }
        first = 0;
      }

      ret = tas_libc_poll(ctx->pollfds_cache, nfds_linux, 0);
      if (ret < 0) {
        perror("tas_poll: mixed poll, linux poll failed");
        return -1;
      }

      mixed_events += ret;
    }
  } while (active_fds == 0 && mixed_events == 0 && timeout != 0 &&
      (timeout == -1 || get_msecs() < mtimeout));

  /* copy events from linux fds over */
  if (mixed_events > 0) {
    for (j = 0, i = 0; i < nfds; i++) {
      p = &fds[i];

      if (flextcp_fd_slookup(p->fd, &s) == 0) {
        /* this is a tas fd */
        flextcp_fd_srelease(p->fd, s);
        continue;
      }

      if (p->fd != ctx->pollfds_cache[j].fd) {
        fprintf(stderr, "tas_poll: fds in poll cache changed? something is "
            "wrong\n");
        abort();
      }

      p->revents = ctx->pollfds_cache[j].revents;
      j++;
    }
  }

  return active_fds + mixed_events;
}

int tas_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p,
    const sigset_t *sigmask)
{
  assert(!"NYI");
  errno = ENOTSUP;
  return -1;
}

static inline uint32_t events_epoll2poll(uint32_t epoll_event)
{
  uint32_t poll_ev = 0;

  if ((epoll_event & EPOLLIN) != 0)
    poll_ev |= POLLIN | POLLRDNORM;
  if ((epoll_event & EPOLLOUT) != 0)
    poll_ev |= POLLOUT | POLLWRNORM;
  if ((epoll_event & EPOLLRDHUP) != 0)
    poll_ev |= POLLRDHUP;
  if ((epoll_event & EPOLLPRI) != 0)
    poll_ev |= POLLPRI;
  if ((epoll_event & EPOLLERR) != 0)
    poll_ev |= POLLERR;
  if ((epoll_event & EPOLLHUP) != 0)
    poll_ev |= POLLHUP;

  return poll_ev;
}

/* make sure that ctx->pollfds_cache can hold at least n entries */
static int pollfd_cache_alloc(struct sockets_context *ctx, size_t n)
{
  void *ptr;
  size_t size, cnt = ctx->pollfds_cache_size;

  if (cnt >= n) {
    return 0;
  }


  /* set initial size to 16 */
  if (cnt == 0)
    cnt = 16;

  /* double size till we have enough to keep it a power of 2 */
  while (cnt < n)
    cnt *= 2;

  size = cnt * sizeof(struct pollfd);
  if ((ptr = realloc(ctx->pollfds_cache, size)) == NULL) {
    perror("pollfd_cache_alloc: alloc failed");
    return -1;
  }

  ctx->pollfds_cache = ptr;
  ctx->pollfds_cache_size = cnt;
  return 0;
}

/* copy subset of linux fds from fds to pollfd cache. check that number matches
 * num_linuxfds_confirm */
static int pollfd_cache_prepare(struct sockets_context *ctx, struct pollfd *fds,
    nfds_t nfds, nfds_t num_linuxfds_confirm)
{
  struct socket *s;
  struct pollfd *p;
  nfds_t i, num_linux = 0;

  if (pollfd_cache_alloc(ctx, num_linuxfds_confirm) != 0)
    return -1;

  /* copy linux fds to pollfds_cache */
  for (i = 0; i < nfds; i++) {
    p = &fds[i];

    if (flextcp_fd_slookup(p->fd, &s) == 0) {
      /* this is a tas fd */
      flextcp_fd_srelease(p->fd, s);
      continue;
    }

    if (num_linux >= num_linuxfds_confirm) {
      fprintf(stderr, "pollfd_cache_prepare: number of linux fds changed?\n");
      return -1;
    }

    ctx->pollfds_cache[num_linux] = *p;
    num_linux++;
  }

  return 0;
}
