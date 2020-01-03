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

#ifndef FLEXTCP_SOCKETS_H_
#define FLEXTCP_SOCKETS_H_

#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

/**
 * @file tas_sockets.h
 * @brief TAS sockets emulation.
 *
 * @addtogroup libtas-sockets
 * @brief TAS sockets emulation library.
 * @{ */

int tas_init(void);

int tas_socket(int domain, int type, int protocol);

int tas_close(int sockfd);

int tas_shutdown(int sockfd, int how);

int tas_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

int tas_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

int tas_listen(int sockfd, int backlog);

int tas_accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    int flags);

int tas_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);


int tas_fcntl(int sockfd, int cmd, ...);

int tas_getsockopt(int sockfd, int level, int optname, void *optval,
    socklen_t *optlen);

int tas_setsockopt(int sockfd, int level, int optname, const void *optval,
    socklen_t optlen);

int tas_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

int tas_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

int tas_move_conn(int sockfd);


ssize_t tas_read(int fd, void *buf, size_t count);

ssize_t tas_recv(int sockfd, void *buf, size_t len, int flags);

ssize_t tas_recvfrom(int sockfd, void *buf, size_t len, int flags,
    struct sockaddr *src_addr, socklen_t *addrlen);

ssize_t tas_recvmsg(int sockfd, struct msghdr *msg, int flags);

ssize_t tas_readv(int sockfd, const struct iovec *iov, int iovcnt);

ssize_t tas_pread(int sockfd, void *buf, size_t count, off_t offset);

ssize_t tas_write(int fd, const void *buf, size_t count);

ssize_t tas_send(int sockfd, const void *buf, size_t len, int flags);

ssize_t tas_sendto(int sockfd, const void *buf, size_t len, int flags,
    const struct sockaddr *dest_addr, socklen_t addrlen);

ssize_t tas_sendmsg(int sockfd, const struct msghdr *msg, int flags);

ssize_t tas_writev(int sockfd, const struct iovec *iov, int iovcnt);

ssize_t tas_pwrite(int sockfd, const void *buf, size_t count, off_t offset);

ssize_t tas_sendfile(int sockfd, int in_fd, off_t *offset, size_t len);


int tas_epoll_create(int size);

int tas_epoll_create1(int flags);

int tas_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);

int tas_epoll_wait(int epfd, struct epoll_event *events, int maxevents,
    int timeout);

int tas_epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
    int timeout, const sigset_t *sigmask);


int tas_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout);

int tas_pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    const struct timespec *timeout, const sigset_t *sigmask);


int tas_poll(struct pollfd *fds, nfds_t nfds, int timeout);

int tas_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p,
    const sigset_t *sigmask);


int tas_dup(int oldfd);

int tas_dup2(int oldfd, int newfd);

int tas_dup3(int oldfd, int newfd, int flags);

/** @} */

#endif /* ndef FLEXTCP_SOCKETS_H_ */
