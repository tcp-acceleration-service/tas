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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <time.h>

int main(int argc, char *argv[])
{
  int listenfd, connfd, efd, flag, ret;
  struct sockaddr_in serv_addr;
  struct epoll_event epe;
  static uint8_t buf[1024];

  if ((efd = epoll_create1(0)) < 0) {
    perror("epoll create failed");
    abort();
  }

  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    abort();
  }

  memset(&serv_addr, '0', sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(1234);

  if (bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
    perror("bind failed");
    abort();
  }

  if (listen(listenfd, 10)) {
    perror("listen failed");
    abort();
  }

  if ((connfd = accept(listenfd, NULL, NULL)) < 0) {
    perror("accept failed");
    abort();
  }
  printf("Accepted connection\n");

  if ((flag = fcntl(connfd, F_GETFL, 0)) == -1) {
    perror("fcntl getfl failed");
    abort();
  }
  flag |= O_NONBLOCK;
  if (fcntl(connfd, F_SETFL, flag) == -1) {
    perror("fcntl setfl failed");
    abort();
  }

  epe.events = EPOLLIN | EPOLLRDHUP;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &epe) != 0) {
    perror("epoll_ctl failed");
    abort();
  }

  while(1) {
    ret = epoll_wait(efd, &epe, 1, -1);
    if (ret < 0) {
      perror("epoll_wait failed");
      abort();
    } else if (ret == 0) {
      continue;
    }

    printf("epoll_wait: IN=%d RDHUP=%d HUP=%d ERR=%d\n",
        !!(epe.events & EPOLLIN),
        !!(epe.events & EPOLLRDHUP),
        !!(epe.events & EPOLLHUP),
        !!(epe.events & EPOLLERR));

    ret = recv(connfd, buf, sizeof(buf), 0);
    printf("recv: ret=%d errno=%d\n", ret, errno);
  }
}
