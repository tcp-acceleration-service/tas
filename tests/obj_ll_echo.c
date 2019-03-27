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
#include <pthread.h>
#include <tas_ll.h>
#include <utils.h>

#define MAX_CORES 32

static volatile int ready = 0;

struct worker {
  struct flextcp_context ctx;
  uint32_t id;
};

static void print_usage(void)
{
  fprintf(stderr, "Usage: obj_ll_echo CORES IP PORT\n");
}

static int init_connect(struct flextcp_context *ctx, uint32_t ip,
    uint16_t port, struct flextcp_obj_connection *conn)
{
  struct flextcp_event ev;
  int ret;

  fprintf(stderr, "flextcp_obj_connection_open\n");
  if (flextcp_obj_connection_open(ctx, conn, ip, port, 0) != 0) {
    fprintf(stderr, "flextcp_obj_connection_open failed\n");
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

    if (ev.event_type != FLEXTCP_EV_OBJ_CONN_OPEN) {
      fprintf(stderr, "init_connect: unexpected event type (%u)\n",
          ev.event_type);
      continue;
    }

    if (ev.ev.obj_conn_open.status != 0) {
      fprintf(stderr, "init_connect: listen open request failed\n");
      return -1;
    }

    break;
  }

  fprintf(stderr, "init_connect: connection succeeded\n");

  return 0;
}

static int init_listen(struct flextcp_context *ctx, uint16_t port,
    struct flextcp_obj_listener *listen, struct flextcp_obj_connection *conn)
{
  struct flextcp_event ev;
  int ret;

  if (flextcp_obj_listen_open(ctx, listen, port, 8, 0) != 0) {
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
    if (ret == 0)  {
      continue;
    }

    if (ev.event_type != FLEXTCP_EV_OBJ_LISTEN_OPEN) {
      fprintf(stderr, "init_listen: unexpected event type (%u)\n",
          ev.event_type);
      continue;
    }

    if (ev.ev.obj_listen_open.status != 0) {
      fprintf(stderr, "init_listen: listen open request failed\n");
      return -1;
    }

    break;
  }

  printf("Sending accept request\n");
  if (flextcp_obj_listen_accept(ctx, listen, conn) != 0) {
    fprintf(stderr, "init_listen: flextcp_listen_accept failed\n");
    return -1;
  }

  return 0;
}

static void *worker_loop(void *arg)
{
  struct worker *w = arg;
  struct flextcp_context *ctx = &w->ctx;
  struct flextcp_obj_connection *conn;
  struct flextcp_event evs[4];
  int num, i;
  size_t j, k, len, l1, off;
  uint8_t *buf;
  void *b1, *b2;
  struct flextcp_obj_handle oh;
  uint32_t id = w->id;

  while (1) {
    num = flextcp_context_poll(&ctx[0], 4, evs);
    for (i = 0; i < num; i++) {
      if (evs[i].event_type == FLEXTCP_EV_OBJ_CONN_RECEIVED) {
        conn = evs[i].ev.obj_conn_received.conn;

        len = evs[i].ev.obj_conn_received.len_1 +
            evs[i].ev.obj_conn_received.len_2;

        if (flextcp_obj_connection_tx_alloc(conn,
            evs[i].ev.obj_conn_received.dstlen,
            len - evs[i].ev.obj_conn_received.dstlen, &b1, &l1, &b2, &oh) != 0)
        {
          fprintf(stderr, "flextcp_obj_connection_tx_alloc failed\n");
          abort();
        }

        /* print received data */
        printf("id=%02u  Data received: dstlen=%u '", id,
            evs[i].ev.obj_conn_received.dstlen);
        off = 0;
        for (k = 0; k < 2; k++) {
          if (k == 0) {
            len = evs[i].ev.obj_conn_received.len_1;
            buf = evs[i].ev.obj_conn_received.buf_1;
          } else {
            len = evs[i].ev.obj_conn_received.len_2;
            buf = evs[i].ev.obj_conn_received.buf_2;
          }

          for (j = 0; j < len; j++) {
            printf("%02x ", buf[j]);
            if (off < l1) {
              ((uint8_t *) b1)[off] = buf[j];
            } else {
              ((uint8_t *) b2)[off - l1] = buf[j];
            }
            off++;
          }
        }
        printf("'\n");

        /* free rx buffer */
        flextcp_obj_connection_rx_done(ctx, conn,
              &evs[i].ev.obj_conn_received.handle);

        /* send out response */
        flextcp_obj_connection_tx_send(ctx, conn, &oh);

        if (flextcp_obj_connection_bump(ctx, conn) != 0) {
          fprintf(stderr, "flextcp_obj_connection_bump failed\n");
          abort();
        }

      } else {
        printf("event: %u\n", evs[i].event_type);
      }
    }
  }



  return NULL;
}

int main(int argc, char *argv[])
{
  struct worker *workers[MAX_CORES], *worker;
  struct flextcp_obj_connection conn;
  struct flextcp_obj_listener listen;
  pthread_t threads[MAX_CORES];
  uint32_t ip;
  uint16_t port, cores;
  int connect = 0;
  size_t j;

  /* parse parameters */
  if (argc < 3 || argc > 4) {
    print_usage();
    return -1;
  }

  cores = atoi(argv[1]);
  if (cores > MAX_CORES || cores < 1) {
    fprintf(stderr, "obj_ll_echo: invalid number of cores (%u, max=%u)\n",
        cores, MAX_CORES);
    return -1;
  }

  if (argc == 4) {
    connect = 1;
    if (util_parse_ipv4(argv[2], &ip) != 0) {
      print_usage();
      return -1;
    }
  }
  port = atoi(argv[argc - 1]);

  /* initialize stack */
  if (flextcp_init()) {
    fprintf(stderr, "flextcp_init failed\n");
    return -1;
  }

  printf("Stack initialized\n");

  /* initialize contexts */
  for (j = 0; j < cores; j++) {
    if ((worker = malloc(sizeof(*worker))) == NULL) {
      fprintf(stderr, "obj_ll_echo: allocating worker %zu failed\n", j);
      return -1;
    }
    workers[j] = worker;

    worker->id = j;
    if (flextcp_context_create(&worker->ctx)) {
      fprintf(stderr, "flextcp_context_create %zu failed\n", j);
      return -1;
    }
  }

  printf("Contexts initialized\n");

  /* open connection */
  if (connect == 0) {
    printf("Listening...\n");
    if (init_listen(&workers[0]->ctx, port, &listen, &conn) != 0) {
      return -1;
    }
    printf("Listened\n");
  } else {
    printf("Connecting...\n");
    if (init_connect(&workers[0]->ctx, ip, port, &conn) != 0) {
      return -1;
    }
    printf("Connected\n");
  }

  /* start worker threads */
  printf("Starting worker threads\n");
  for (j = 1; j < cores; j++) {
    if (pthread_create(threads + j, NULL, worker_loop, workers[j]) != 0) {
      fprintf(stderr, "obj_ll_echo: pthread_create %zu failed\n", j);
      return -1;
    }
  }
  printf("Started worker threads\n");

  worker_loop(workers[0]);

  return 0;
}
