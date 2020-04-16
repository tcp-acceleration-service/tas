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
  struct flextcp_context *ctx;
  struct socket *s;
  struct pollfd *p;
  nfds_t nfds_linux, nfds_tas, i;
  uint32_t s_events;
  int active_fds = 0;
  uint64_t mtimeout = 0;

  ctx = flextcp_sockctx_get();

  if (timeout != 0 && timeout != -1)
    mtimeout = get_msecs() + timeout;

  do {
    flextcp_sockctx_poll_n(ctx, nfds);

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
       * fds*/
      assert(!"NYI");
      errno = ENOTSUP;
      return -1;
    }
  } while (active_fds == 0 && timeout != 0 &&
      (timeout == -1 || get_msecs() < mtimeout));

  return active_fds;
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
