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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tas_ll.h>
#include <utils.h>

static void print_usage(void)
{
  fprintf(stderr, "Usage: lowlevel_echo IP PORT\n");
}

static int init_connect(struct flextcp_context *ctx, uint32_t ip,
    uint16_t port, struct flextcp_connection *conn)
{
  struct flextcp_event ev;
  int ret;

  if (flextcp_connection_open(ctx, conn, ip, port) != 0) {
    fprintf(stderr, "flextcp_connection_open failed\n");
    return -1;
  }

  /* wait for connection to open */
  while (1) {
    if ((ret = flextcp_context_poll(ctx, 1, &ev)) < 0) {
      fprintf(stderr, "init_connect: flextcp_context_poll failed\n");
      return -1;
    }

    /* skip if no event */
    if (ret == 0) {
      continue;
    }

    if (ev.event_type != FLEXTCP_EV_CONN_OPEN) {
      fprintf(stderr, "init_connect: unexpected event type (%u)\n",
          ev.event_type);
      continue;
    }

    if (ev.ev.conn_open.status != 0) {
      fprintf(stderr, "init_connect: listen open request failed\n");
      return -1;
    }

    break;
  }

  return 0;
}

static int init_listen(struct flextcp_context *ctx, uint16_t port,
    struct flextcp_listener *listen, struct flextcp_connection *conn)
{
  struct flextcp_event ev;
  int ret;

  if (flextcp_listen_open(ctx, listen, port, 8, 0) != 0) {
    fprintf(stderr, "init_listen: flextcp_listen_open failed\n");
    return -1;
  }

  printf("Wait for listen response\n");

  /* wait until listen request is done */
  while (1) {
    if ((ret = flextcp_context_poll(ctx, 1, &ev)) < 0) {
      fprintf(stderr, "init_listen: flextcp_context_poll failed\n");
      return -1;
    }

    /* skip if no event */
    if (ret == 0) {
      continue;
    }

    if (ev.event_type != FLEXTCP_EV_LISTEN_OPEN) {
      fprintf(stderr, "init_listen: unexpected event type (%u)\n",
          ev.event_type);
      continue;
    }

    if (ev.ev.listen_open.status != 0) {
      fprintf(stderr, "init_listen: listen open request failed\n");
      return -1;
    }

    break;
  }

  printf("Sending accept request\n");
  if (flextcp_listen_accept(ctx, listen, conn) != 0) {
    fprintf(stderr, "init_listen: flextcp_listen_accept failed\n");
    return -1;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  struct flextcp_context ctx;
  struct flextcp_connection conn;
  struct flextcp_listener listen;
  uint32_t ip;
  struct flextcp_event evs[4];
  uint16_t port;
  uint8_t type;
  int num, i, connect = 0, closed = 0;
  size_t j, len;
  ssize_t res;
  void *txbuf;

  /* parse parameters */
  if (argc < 2 || argc > 3) {
    print_usage();
    return -1;
  }
  if (argc == 3) {
    connect = 1;
    if (util_parse_ipv4(argv[1], &ip) != 0) {
      print_usage();
      return -1;
    }
  }
  port = atoi(argv[argc - 1]);

  if (flextcp_init()) {
    fprintf(stderr, "flextcp_init failed\n");
    return -1;
  }

  if (flextcp_context_create(&ctx)) {
    fprintf(stderr, "flextcp_context_create failed\n");
    return -1;
  }

  if (connect == 0) {
    if (init_listen(&ctx, port, &listen, &conn) != 0) {
      return -1;
    }
  } else {
    if (init_connect(&ctx, ip, port, &conn) != 0) {
      return -1;
    }
  }

  while (1) {
    num = flextcp_context_poll(&ctx, 4, evs);
    for (i = 0; i < num; i++) {
      type = evs[i].event_type;

      if (type == FLEXTCP_EV_CONN_RECEIVED) {
        len = evs[i].ev.conn_received.len;

        /* print received data */
        printf("Data received:'");
        for (j = 0; j < len; j++) {
          printf("%c", ((char *) evs[i].ev.conn_received.buf)[j]);
        }
        printf("'\n");

        /* allocate tx buffer for echoing */
        res = flextcp_connection_tx_alloc(&conn, len, &txbuf);
        if (res < 0) {
          fprintf(stderr, "flextcp_connection_tx_alloc failed\n");
          return -1;
        } else if (res < (ssize_t) len) {
          printf("Short alloc: %llu instead of %llu bytes",
              (unsigned long long) res, (unsigned long long) len);
        }

        /* copy payload into tx buffer */
        memcpy(txbuf, evs[i].ev.conn_received.buf, res);

        if (flextcp_connection_tx_send(&ctx, &conn, res) != 0) {
          fprintf(stderr, "flextcp_connection_tx_send failed\n");
          return -1;
        }

        /* free rx buffer */
        if (flextcp_connection_rx_done(&ctx, &conn, len)
            != 0)
        {
          fprintf(stderr, "flextcp_connection_rx_done failed\n");
          return -1;
        }

        if (!closed) {
          if (flextcp_connection_tx_close(&ctx, &conn) != 0) {
            fprintf(stderr, "flextcp_connection_tx_close failed\n");
            return -1;
          }
          closed = 1;
        }

      } else if (type == FLEXTCP_EV_CONN_RXCLOSED) {
        printf("RX EOS received\n");
        if (!closed) {
          if (flextcp_connection_tx_close(&ctx, &conn) != 0) {
            fprintf(stderr, "flextcp_connection_tx_close failed\n");
            return -1;
          }
          closed = 1;
        }
      } else if (type == FLEXTCP_EV_CONN_TXCLOSED) {
        printf("TX EOS received\n");
      } else {
        printf("event: %u\n", evs[i].event_type);
      }
    }
  }

  return 0;
}
