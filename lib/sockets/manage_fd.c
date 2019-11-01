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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <utils.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include "internal.h"
#include <tas_sockets.h>

#define MAXSOCK 1024 * 1024

enum fh_type {
  FH_UNUSED,
  FH_SOCKET,
  FH_EPOLL,
};

struct filehandle {
  union {
    struct socket *s;
    struct epoll *e;
  } data;
  uint8_t type;
};

static struct filehandle fhs[MAXSOCK];

int flextcp_fd_init(void)
{
  return 0;
}


int flextcp_fd_salloc(struct socket **ps)
{
  struct socket *s;
  int fd;

  if ((s = calloc(1, sizeof(*s))) == NULL) {
    errno = ENOMEM;
    return -1;
  }

  /* get eventfd so we reserve the FD in the kernel to avoid overlap */
  if ((fd = eventfd(0, 0)) < 0) {
    free(s);
    return -1;
  }

  /* no more file handles available */
  if (fd >= MAXSOCK) {
    free(s);
    tas_libc_close(fd);
    errno = EMFILE;
    return -1;
  }

  s->type = SOCK_SOCKET;
  s->refcnt = 1;
  s->sp_lock = 1;

  fhs[fd].data.s = s;
  fhs[fd].type = FH_SOCKET;

  *ps = s;

  return fd;
}

int flextcp_fd_slookup(int fd, struct socket **ps)
{
  struct socket *s;

  if (fd >= MAXSOCK || fhs[fd].type != FH_SOCKET) {
    errno = EBADF;
    return -1;
  }

  s = fhs[fd].data.s;
  socket_lock(s);
  *ps = s;
  return 0;
}

int flextcp_fd_ealloc(struct epoll **pe, int fd)
{
  struct epoll *e;

  /* no more file handles available */
  if (fd >= MAXSOCK) {
    errno = EMFILE;
    return -1;
  }

  assert(fhs[fd].type == FH_UNUSED);

  if ((e = calloc(1, sizeof(*e))) == NULL) {
    errno = ENOMEM;
    return -1;
  }

  e->refcnt = 1;
  e->sp_lock = 1;

  fhs[fd].data.e = e;
  fhs[fd].type = FH_EPOLL;

  *pe = e;

  return fd;
}

int flextcp_fd_elookup(int fd, struct epoll **pe)
{
  struct epoll *e;

  if (fd >= MAXSOCK || fhs[fd].type != FH_EPOLL) {
    errno = EBADF;
    return -1;
  }

  e = fhs[fd].data.e;
  epoll_lock(e);
  *pe = e;
  return 0;
}

void flextcp_fd_srelease(int fd, struct socket *s)
{
  socket_unlock(s);
}

void flextcp_fd_erelease(int fd, struct epoll *e)
{
  epoll_unlock(e);
}

void flextcp_fd_close(int fd)
{
  assert(fhs[fd].type == FH_SOCKET || fhs[fd].type == FH_EPOLL);
  if (fhs[fd].type == FH_SOCKET) {
    fhs[fd].data.s->refcnt--;
    fhs[fd].data.s = NULL;
  } else if (fhs[fd].type == FH_EPOLL) {
    fhs[fd].data.e->refcnt--;
    fhs[fd].data.e = NULL;
  } else {
    fprintf(stderr, "flextcp_fd_close: trying to close non-opened tas fd\n");
    abort();
  }

  fhs[fd].type = FH_UNUSED;
  MEM_BARRIER();
  tas_libc_close(fd);
}

/* do the tas-internal part of duping oldfd to newfd, after the linux fds have
 * already been dup'd */
static inline int internal_dup3(int oldfd, int newfd, int flags)
{
  struct socket *s;
  struct epoll *ep;

  /* TODO: check flags */

  if (newfd >= MAXSOCK) {
    fprintf(stderr, "tas_dup: failed because new fd is larger than MAXSOCK\n");
    abort();
  }

  /* close any previous socket or epoll at newfd */
  if (fhs[newfd].type  == FH_SOCKET) {
    s = fhs[newfd].data.s;

    /* close socket */
    socket_lock(s);
    s->refcnt--;
    if (s->refcnt == 0)
      tas_sock_close(s);
    else
      socket_unlock(s);

    fhs[newfd].data.s = NULL;
    fhs[newfd].type = FH_UNUSED;
  } else if (fhs[newfd].type  == FH_EPOLL) {
    ep = fhs[newfd].data.e;

    /* close epoll */
    epoll_lock(ep);
    ep->refcnt--;
    if (ep->refcnt == 0)
      flextcp_epoll_destroy(ep);
    else
      epoll_unlock(ep);

    fhs[newfd].data.e = NULL;
    fhs[newfd].type = FH_UNUSED;
  }

  /* next dup the underlying TAS socket and epoll if necessary */
  if (flextcp_fd_slookup(oldfd, &s) == 0) {
    /* oldfd is a tas socket */
    fhs[newfd].type = FH_SOCKET;
    fhs[newfd].data.s = s;

    s->refcnt++;

    flextcp_fd_srelease(oldfd, s);
  } else if (flextcp_fd_elookup(oldfd, &ep) == 0) {
    /* oldfd is a tas epoll */
    fhs[newfd].type = FH_EPOLL;
    fhs[newfd].data.e = ep;

    ep->refcnt++;

    flextcp_fd_srelease(oldfd, s);
  }

  return newfd;
}

int tas_dup(int oldfd)
{
  int newfd;

  /* either way we want to dup the linux fd */
  newfd = tas_libc_dup(oldfd);
  if (newfd < 0)
    return newfd;

  return internal_dup3(oldfd, newfd, 0);
}

int tas_dup2(int oldfd, int newfd)
{
  /* either way we want to dup the linux fd */
  newfd = tas_libc_dup2(oldfd, newfd);
  if (newfd < 0)
    return newfd;

  /* for dup2 this acts as a nop */
  if (newfd == oldfd)
    return newfd;

  return internal_dup3(oldfd, newfd, 0);
}

int tas_dup3(int oldfd, int newfd, int flags)
{
  /* either way we want to dup the linux fd */
  newfd = tas_libc_dup3(oldfd, newfd, flags);
  if (newfd < 0)
    return newfd;

  return internal_dup3(oldfd, newfd, flags);
}
