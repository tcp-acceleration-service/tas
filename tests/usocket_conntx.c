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
#include <netdb.h>
#include <tas_sockets.h>

int main(int argc, char *argv[])
{
  int fd, ret, i;
  struct addrinfo hints;
  struct addrinfo *res;
  struct iovec iov[3];
  char buf1[1] = "H";
  char buf2[5] = "ello ";
  char buf3[7] = "World!\n";
  struct msghdr msg;

  if (argc != 3) {
    fprintf(stderr, "Usage: usocket_connect IP PORT\n");
    return -1;
  }

  if (tas_init() != 0) {
    perror("tas_init failed");
    return -1;
  }

  /* parse address */
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(argv[1], argv[2], &hints, &res) != 0) {
    perror("getaddrinfo failed");
    return -1;
  }

  /* open socket */
  if ((fd = tas_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    abort();
  }

  /* connect socket */
  if (tas_connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
    perror("connect failed");
    abort();
  }

  for (i = 0; i < 10; i++) {
    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1);
    iov[1].iov_base = buf2;
    iov[1].iov_len = sizeof(buf2);
    iov[2].iov_base = buf3;
    iov[2].iov_len = sizeof(buf3);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    ret = tas_sendmsg(fd, &msg, 0);
    if (ret < 0) {
      perror("sendmsg failed");
      abort();
    }
    if (ret != 13) {
      fprintf(stderr, "sendmsg returned %u (expected 13) :-/\n",
          (unsigned) ret);
      abort();
    }
  }

  tas_close(fd);
  return 0;
}
