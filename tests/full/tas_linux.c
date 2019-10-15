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

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>

#include <tas_ll.h>

#include "../testutils.h"
#include "fulltest.h"

static int test_1_tas(void *data)
{
  int nret;

  /* connect to tas */
  if (flextcp_init() != 0) {
    fprintf(stderr, "flextcp_init failed\n");
    return 1;
  }

  /* create context */
  struct flextcp_context context;
  if (flextcp_context_create(&context) != 0) {
    fprintf(stderr, "flextcp_context_create failed\n");
    return 1;
  }

  /* prepare listener */
  struct flextcp_listener listen;
  if (flextcp_listen_open(&context, &listen, 1234, 32,
              FLEXTCP_LISTEN_REUSEPORT) != 0)
  {
    fprintf(stderr, "flextcp_listen_open failed\n");
    return 1;
  }

  /* wait for listener to open */
  struct flextcp_event evs[32];
  int n;
  do {
    if ((n = flextcp_context_poll(&context, 1, evs)) < 0) {
      fprintf(stderr, "flextcp_context_poll failed\n");
      return 1;
    }

    if (n == 0)
      continue;

    if (n == 1 && evs[0].event_type == FLEXTCP_EV_LISTEN_OPEN &&
        evs[0].ev.listen_open.status == 0)
      break;

    fprintf(stderr, "unexpected event: %u\n", evs[0].event_type);
    return 1;
  } while (1);

  /* create linux socket */
  int sock;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    return 1;
  }

  /* set socket to nonblocking */
  int flag;
  if ((flag = fcntl(sock, F_GETFL, 0)) == -1) {
    perror("fcntl getfl failed");
    return 1;
  }
  flag |= O_NONBLOCK;
  if (fcntl(sock, F_SETFL, flag) == -1) {
    perror("fcntl setfl failed");
    return 1;
  }

  /* connect to listener */
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(0xc0a80101);
  addr.sin_port = htons(1234);
  if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) != -1 && errno == EINPROGRESS) {
    perror("connect failed");
    return 1;
  }

  /* wait for newconn event */
  do {
    if ((n = flextcp_context_poll(&context, 1, evs)) < 0) {
      fprintf(stderr, "flextcp_context_poll failed\n");
      return 1;
    }

    if (n == 0)
      continue;

    if (n == 1 && evs[0].event_type == FLEXTCP_EV_LISTEN_NEWCONN)
      break;

    fprintf(stderr, "unexpected event: %u\n", evs[0].event_type);
    return 1;
  } while (1);

  /* accept connection */
  struct flextcp_connection conn;
  if (flextcp_listen_accept(&context, &listen, &conn) != 0) {
    fprintf(stderr, "accept failed");
    return 1;
  }

  /* wait for accepted event */
  do {
    if ((n = flextcp_context_poll(&context, 1, evs)) < 0) {
      fprintf(stderr, "flextcp_context_poll failed\n");
      return 1;
    }

    if (n == 0)
      continue;

    if (n == 1 && evs[0].event_type == FLEXTCP_EV_LISTEN_ACCEPT &&
        evs[0].ev.listen_accept.status == 0)
      break;

    fprintf(stderr, "unexpected event: %u\n", evs[0].event_type);
    return 1;
  } while (1);

  /* wait for connection to be established */
  socklen_t slen;
  do {
    slen = sizeof(nret);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &nret, &slen) != 0) {
      perror("getsockopt failed");
      return 1;
    }

    if (nret == 0)
      break;
  } while (1);

  /* send some data */
  static char buffer[1024 * 32];
  ssize_t data_sent;
  do {
    data_sent = write(sock, buffer, sizeof(buffer));
  } while (data_sent == -1 && errno == EAGAIN);
  if (data_sent == -1) {
    perror("send failed");
    return 1;
  }

  /* wait for data event */
  ssize_t data_rxd = 0;

  while (data_rxd < data_sent) {
    time_t start_time = time(NULL);
    size_t rx_done = 0;
    do {
      if ((n = flextcp_context_poll(&context, 32, evs)) < 0) {
        fprintf(stderr, "flextcp_context_poll failed\n");
        return 1;
      }

      int i;
      for (i = 0; i < n; i++) {
        if (evs[i].event_type != FLEXTCP_EV_CONN_RECEIVED) {
          fprintf(stderr, "unexpected event: %u\n", evs[i].event_type);
          return 1;
        }

        rx_done += evs[i].ev.conn_received.len;
      }
    } while (time(NULL) - start_time < 1);

    if (flextcp_connection_rx_done(&context, &conn, rx_done) != 0) {
      fprintf(stderr, "flextcp_connection_rx_done failed\n");
      return 1;
    }

    data_rxd += rx_done;
  }

  fprintf(stderr, "success\n");
  return 0;
}

static int test_1_linux(void *data)
{
  return 0;
}

int main(int argc, char *argv[])
{
  return full_testcase(test_1_tas, test_1_linux, NULL);
}
