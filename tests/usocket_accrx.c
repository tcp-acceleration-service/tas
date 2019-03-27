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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <tas_sockets.h>

int main(int argc, char *argv[])
{
  int listenfd = 0, connfd = 0;
  struct sockaddr_in serv_addr;
  ssize_t ret;
  struct iovec iov[3];
  char buf1[4];
  char buf2[4];
  char buf3[4];
  struct msghdr msg;

  if (tas_init() != 0) {
    perror("tas_init failed");
    return -1;
  }

  if ((listenfd = tas_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    abort();
  }

  memset(&serv_addr, '0', sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(1234);

  if (tas_bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
    perror("bind failed");
    abort();
  }

  if (tas_listen(listenfd, 10)) {
    perror("listen failed");
    abort();
  }

  if ((connfd = tas_accept(listenfd, NULL, NULL)) < 0) {
    perror("accept failed");
    abort();
  }

  while(1) {
    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1);
    iov[1].iov_base = buf2;
    iov[1].iov_len = sizeof(buf2);
    iov[2].iov_base = buf3;
    iov[2].iov_len = sizeof(buf3);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    ret = tas_recvmsg(connfd, &msg, 0);
    if (ret < 0) {
      perror("recvmsg failed");
      abort();
    }
    if (ret == 0) {
      fprintf(stderr, "recvmsg returned 0 :-/\n");
      abort();
    }
    if (ret > 8) {
      printf("%.*s%.*s%.*s", 4, buf1, 4, buf2, (int) (ret - 8), buf3);
    } else if (ret > 4) {
      printf("%.*s%.*s", 4, buf1, (int) (ret - 4), buf2);
    } else {
      printf("%.*s", (int) ret, buf1);
    }
  }
}
