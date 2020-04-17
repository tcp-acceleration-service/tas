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
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>

#include <utils.h>
#include <utils_circ.h>
#include <tas_sockets.h>
#include <tas_ll.h>

#include "internal.h"
#include "../tas/internal.h"

ssize_t tas_recvmsg(int sockfd, struct msghdr *msg, int flags)
{
  struct socket *s;
  struct flextcp_context *ctx;
  ssize_t ret = 0;
  size_t len, i, off;
  struct iovec *iov;
  int block;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  /* not a connection, or not connected */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  /* return 0 if 0 length */
  len = 0;
  iov = msg->msg_iov;
  for (i = 0; i < msg->msg_iovlen; i++) {
    len += iov[i].iov_len;
  }
  if (len == 0) {
    goto out;
  }

  ctx = flextcp_sockctx_get();

  /* wait for data if necessary, or abort after polling once if non-blocking */
  block = 0;
  while (s->data.connection.rx_len_1 == 0 &&
      !(s->data.connection.st_flags & CSTF_RXCLOSED))
  {
    flextcp_epoll_clear(s, EPOLLIN);

    /* even if non-blocking we have to poll the context at least once to handle
     * busy polling loops of recvmsg */
    socket_unlock(s);
    if (block)
      flextcp_context_wait(ctx, -1);
    block = 1;
    flextcp_sockctx_poll(ctx);
    socket_lock(s);

    /* if non-blocking and nothing then we abort now */
    if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK &&
        s->data.connection.rx_len_1 == 0 &&
        !(s->data.connection.st_flags & CSTF_RXCLOSED))
    {
      errno = EAGAIN;
      ret = -1;
      goto out;
    }
  }

  /* copy data into buffer vector */
  for (i = 0; i < msg->msg_iovlen && s->data.connection.rx_len_1 > 0; i++) {
    off = 0;
    if (s->data.connection.rx_len_1 <= iov[i].iov_len) {
      off = s->data.connection.rx_len_1;
      memcpy(iov[i].iov_base, s->data.connection.rx_buf_1, off);
      ret += off;

      s->data.connection.rx_buf_1 = s->data.connection.rx_buf_2;
      s->data.connection.rx_len_1 = s->data.connection.rx_len_2;
      s->data.connection.rx_buf_2 = NULL;
      s->data.connection.rx_len_2 = 0;
    }

    len = MIN(iov[i].iov_len - off, s->data.connection.rx_len_1);
    memcpy((uint8_t *) iov[i].iov_base + off, s->data.connection.rx_buf_1, len);
    ret += len;

    s->data.connection.rx_buf_1 = (uint8_t *) s->data.connection.rx_buf_1 + len;
    s->data.connection.rx_len_1 -= len;
  }

  if (ret > 0) {
    if (s->data.connection.rx_len_1 == 0 &&
        !(s->data.connection.st_flags & CSTF_RXCLOSED))
    {
      flextcp_epoll_clear(s, EPOLLIN);
    }
    flextcp_connection_rx_done(ctx, &s->data.connection.c, ret);
  }
out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

static inline ssize_t recv_simple(int sockfd, void *buf, size_t len, int flags)
{
  struct socket *s;
  struct flextcp_context *ctx;
  ssize_t ret = 0;
  size_t off, len_2;
  int block;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  /* not a connection, or not connected */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  /* return 0 if 0 length */
  if (len == 0) {
    goto out;
  }

  ctx = flextcp_sockctx_get();

  /* wait for data if necessary, or abort after polling once if non-blocking */
  block = 0;
  while (s->data.connection.rx_len_1 == 0 &&
      !(s->data.connection.st_flags & CSTF_RXCLOSED))
  {
    flextcp_epoll_clear(s, EPOLLIN);

    /* even if non-blocking we have to poll the context at least once to handle
     * busy polling loops of recvmsg */
    socket_unlock(s);
    if (block)
      flextcp_context_wait(ctx, -1);
    block = 1;
    flextcp_sockctx_poll(ctx);
    socket_lock(s);

    /* if non-blocking and nothing then we abort now */
    if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK &&
        s->data.connection.rx_len_1 == 0 &&
        !(s->data.connection.st_flags & CSTF_RXCLOSED))
    {
      errno = EAGAIN;
      ret = -1;
      goto out;
    }
  }

  /* copy to provided buffer */
  off = 0;
  if (s->data.connection.rx_len_1 <= len) {
    memcpy(buf, s->data.connection.rx_buf_1, s->data.connection.rx_len_1);
    ret = off = s->data.connection.rx_len_1;

    s->data.connection.rx_buf_1 = s->data.connection.rx_buf_2;
    s->data.connection.rx_len_1 = s->data.connection.rx_len_2;
    s->data.connection.rx_buf_2 = NULL;
    s->data.connection.rx_len_2 = 0;
  }
  len_2 = MIN(s->data.connection.rx_len_1, len - off);
  memcpy((uint8_t *) buf + ret, s->data.connection.rx_buf_1, len_2);
  ret += len_2;
  s->data.connection.rx_buf_1 += len_2;
  s->data.connection.rx_len_1 -= len_2;

  if (ret > 0) {
    if (s->data.connection.rx_len_1 == 0 &&
        !(s->data.connection.st_flags & CSTF_RXCLOSED))
    {
      flextcp_epoll_clear(s, EPOLLIN);
    }
    flextcp_connection_rx_done(ctx, &s->data.connection.c, ret);
  }
out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

#include <unistd.h>

ssize_t tas_sendmsg(int sockfd, const struct msghdr *msg, int flags)
{

  struct socket *s;
  struct flextcp_context *ctx;
  ssize_t ret = 0;
  size_t len, i, l, len_1, len_2, off;
  struct iovec *iov;
  void *dst_1, *dst_2;
  int block;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  /* not a connection, or not connected */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED ||
      (s->data.connection.st_flags & CSTF_TXCLOSED) == CSTF_TXCLOSED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  /* return 0 if 0 length */
  len = 0;
  iov = msg->msg_iov;
  for (i = 0; i < msg->msg_iovlen; i++) {
    len += iov[i].iov_len;
  }
  if (len == 0) {
    goto out;
  }

  ctx = flextcp_sockctx_get();

  /* make sure there is space in the transmit queue if the socket is
   * non-blocking */
  if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK &&
      flextcp_connection_tx_possible(ctx, &s->data.connection.c) != 0)
  {
    errno = EAGAIN;
    ret = -1;
    goto out;
  }

  /* allocate transmit buffer */
  ret = flextcp_connection_tx_alloc2(&s->data.connection.c, len, &dst_1, &len_1,
      &dst_2);
  if (ret < 0) {
    fprintf(stderr, "sendmsg: flextcp_connection_tx_alloc failed\n");
    abort();
  }

  /* if tx buffer allocation failed, either block or poll context at least once
   * to handle busy loops of send on non-blocking sockets. */
  block = 0;
  while (ret == 0) {
    socket_unlock(s);
    if (block)
      flextcp_context_wait(ctx, -1);
    block = 1;

    flextcp_sockctx_poll(ctx);
    socket_lock(s);

    ret = flextcp_connection_tx_alloc2(&s->data.connection.c, len, &dst_1,
        &len_1, &dst_2);
    if (ret < 0) {
      fprintf(stderr, "sendmsg: flextcp_connection_tx_alloc failed\n");
      abort();
    } else if (ret == 0 && (s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
      errno = EAGAIN;
      ret = -1;
      goto out;
    }
  }
  len_2 = ret - len_1;

  /* copy into TX buffer */
  len = ret;
  iov = msg->msg_iov;
  off = 0;
  for (i = 0; i < msg->msg_iovlen && len > 0; i++) {
    l = MIN(len, iov[i].iov_len);
    split_write(iov[i].iov_base, l, dst_1, len_1, dst_2, len_2, off);

    len -= l;
    off += l;
  }

  /* send out */
  /* TODO: this should not block for non-blocking sockets */
  block = 0;
  while (flextcp_connection_tx_send(ctx, &s->data.connection.c, ret) != 0) {
    socket_unlock(s);
    if (block)
      flextcp_context_wait(ctx, -1);
    block = 1;

    flextcp_sockctx_poll(ctx);
    socket_lock(s);
  }

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

static inline ssize_t send_simple(int sockfd, const void *buf, size_t len,
    int flags)
{
  struct socket *s;
  struct flextcp_context *ctx;
  ssize_t ret = 0;
  size_t len_1, len_2;
  void *dst_1, *dst_2;
  int block;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  /* not a connection, or not connected */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED ||
      (s->data.connection.st_flags & CSTF_TXCLOSED) == CSTF_TXCLOSED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  /* return 0 if 0 length */
  if (len == 0) {
    goto out;
  }

  ctx = flextcp_sockctx_get();

  /* make sure there is space in the transmit queue if the socket is
   * non-blocking */
  if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK &&
      flextcp_connection_tx_possible(ctx, &s->data.connection.c) != 0)
  {
    errno = EAGAIN;
    ret = -1;
    goto out;
  }

  /* allocate transmit buffer */
  ret = flextcp_connection_tx_alloc2(&s->data.connection.c, len, &dst_1, &len_1,
      &dst_2);
  if (ret < 0) {
    fprintf(stderr, "sendmsg: flextcp_connection_tx_alloc failed\n");
    abort();
  }

  /* if tx buffer allocation failed, either block or poll context at least once
   * to handle busy loops of send on non-blocking sockets. */
  block = 0;
  while (ret == 0) {
    socket_unlock(s);
    if (block)
      flextcp_context_wait(ctx, -1);
    block = 1;

    flextcp_sockctx_poll(ctx);
    socket_lock(s);

    ret = flextcp_connection_tx_alloc2(&s->data.connection.c, len, &dst_1,
        &len_1, &dst_2);
    if (ret < 0) {
      fprintf(stderr, "sendmsg: flextcp_connection_tx_alloc failed\n");
      abort();
    } else if (ret == 0 && (s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
      errno = EAGAIN;
      ret = -1;
      goto out;
    }
  }
  len_2 = ret - len_1;

  /* copy into TX buffer */
  memcpy(dst_1, buf, len_1);
  memcpy(dst_2, (const uint8_t *) buf + len_1, len_2);

  /* send out */
  /* TODO: this should not block for non-blocking sockets */
  block = 0;
  while (flextcp_connection_tx_send(ctx, &s->data.connection.c, ret) != 0) {
    socket_unlock(s);
    if (block)
      flextcp_context_wait(ctx, -1);
    block = 1;

    flextcp_sockctx_poll(ctx);
    socket_lock(s);
  }

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

/******************************************************************************/
/* map:
 *   - read, recv, recvfrom  -->  recvmsg
 *   - write, send, sendto  -->  sendmsg
 */

ssize_t tas_read(int sockfd, void *buf, size_t len)
{
  return recv_simple(sockfd, buf, len, 0);
}

ssize_t tas_recv(int sockfd, void *buf, size_t len, int flags)
{
  return recv_simple(sockfd, buf, len, flags);
}

ssize_t tas_recvfrom(int sockfd, void *buf, size_t len, int flags,
    struct sockaddr *src_addr, socklen_t *addrlen)
{
  ssize_t ret;

  ret = recv_simple(sockfd, buf, len, flags);

  if (src_addr != NULL) {
    *addrlen = *addrlen;
  }
  return ret;
}

ssize_t tas_readv(int sockfd, const struct iovec *iov, int iovlen)
{
  struct msghdr msg;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = (struct iovec *) iov;
  msg.msg_iovlen = iovlen;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  return tas_recvmsg(sockfd, &msg, 0);
}

ssize_t tas_pread(int sockfd, void *buf, size_t len, off_t offset)
{
  /* skipping zero chet for offset here */
  return recv_simple(sockfd, buf, len, 0);
}


ssize_t tas_write(int sockfd, const void *buf, size_t len)
{
  return send_simple(sockfd, buf, len, 0);
}

ssize_t tas_send(int sockfd, const void *buf, size_t len, int flags)
{
  return send_simple(sockfd, buf, len, flags);
}

ssize_t tas_sendto(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen)
{
  return send_simple(sockfd, buf, len, flags);
}

ssize_t tas_writev(int sockfd, const struct iovec *iov, int iovlen)
{
  struct msghdr msg;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = (struct iovec *) iov;
  msg.msg_iovlen = iovlen;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  return tas_sendmsg(sockfd, &msg, 0);
}

ssize_t tas_pwrite(int sockfd, const void *buf, size_t len, off_t offset)
{
  /* skipping zero chet for offset here */
  return send_simple(sockfd, buf, len, 0);
}

ssize_t tas_sendfile(int sockfd, int in_fd, off_t *offset, size_t len)
{
  assert(!"NYI");
  errno = ENOTSUP;
  return -1;
}
