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
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <tas_ll.h>
#include <utils.h>

#define PRINT_STATS
#ifdef PRINT_STATS
#   define STATS_ADD(c, f, n) __sync_fetch_and_add(&c->f, n)
#   define STATS_TS(n) uint64_t n = get_nanos()
#else
#   define STATS_ADD(c, f, n) do { } while (0)
#   define STATS_TS(n) do { } while (0)
#endif

static uint32_t max_flows = 4096;
static uint32_t max_bytes = 1024;
static uint16_t max_events = 64;
static uint16_t listen_port;

struct connection {
    struct flextcp_connection conn;
    struct connection *next;
    uint32_t to_alloc;
    uint32_t to_send;
};

struct core {
    struct flextcp_context context;
    struct flextcp_listener listen;
    struct connection *conns;
    int cn;
#ifdef PRINT_STATS
    uint64_t rx_events;
    uint64_t rx_bytes;
    uint64_t tx_acalls;
    uint64_t tx_scalls;
    uint64_t tx_afail;
    uint64_t tx_sfail;
    uint64_t tx_bytes;
    uint64_t *poll_hist;
#endif
} __attribute__((aligned((64))));

static inline uint64_t get_nanos(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
}

#ifdef PRINT_STATS
static inline uint64_t read_cnt(uint64_t *p)
{
  uint64_t v = *p;
  __sync_fetch_and_sub(p, v);
  return v;
}
#endif

static void prepare_core(struct core *c)
{
    int i, cn = c->cn;
    struct connection *co;

#ifdef PRINT_STATS
    if ((c->poll_hist = calloc(max_events + 1, sizeof(*c->poll_hist)))
        == NULL)
    {
      fprintf(stderr, "[%d] calloc for poll_hist failed\n", cn);
      abort();
    }
#endif

    /* prepare listener */
    if (flextcp_listen_open(&c->context, &c->listen, listen_port, max_flows,
                FLEXTCP_LISTEN_REUSEPORT) != 0)
    {
        fprintf(stderr, "[%d] flextcp_listen_open failed\n", cn);
        abort();
    }


    c->conns = NULL;
    for (i = 0; i < max_flows; i++) {
        /* allocate connection structs */
        if ((co = calloc(1, sizeof(*co))) == NULL) {
            fprintf(stderr, "[%d] alloc of connection structs failed\n", cn);
            abort();
        }

        co->next = c->conns;
        c->conns = co;
    }
}

static inline void accept_connection(struct core *co)
{
    struct connection *c;

    c = co->conns;
    if (c == NULL) {
        fprintf(stderr, "[%d] no connection struct available for new conn\n",
                co->cn);
        return;
    }

    if (flextcp_listen_accept(&co->context, &co->listen, &c->conn) != 0) {
        fprintf(stderr, "[%d] flextcp_listen_accept failed\n", co->cn);
        return;
    }
    co->conns = c->next;
}

static inline void accepted_connection(struct core *co,
        struct flextcp_event *ev)
{
    struct connection *c = (struct connection *) ev->ev.listen_accept.conn;

    if (ev->ev.listen_accept.status != 0) {
        fprintf(stderr, "[%d] flextcp_listen_accept async failure\n", co->cn);
        c->next = co->conns;
        co->conns = c;
        return;
    }

    c->to_send = 0;
    c->to_alloc = 0;
}

static inline int conn_send(struct core *co, struct connection *c)
{
    ssize_t ret;
    size_t allocd = 0;
    void *buf;

    while (allocd < c->to_alloc) {
        STATS_ADD(co, tx_acalls, 1);
        ret = flextcp_connection_tx_alloc(&c->conn, c->to_alloc - allocd, &buf);
        if (ret <= 0) {
            break;
        }
        allocd += ret;
    }
    c->to_alloc -= allocd;
    c->to_send  += allocd;

    if (allocd > 0) {
        flextcp_connection_rx_done(&co->context, &c->conn, allocd);
    }
    if (c->to_alloc > 0) {
        STATS_ADD(co, tx_afail, 1);
    }

    if (c->to_send > 0) {
        STATS_ADD(co, tx_scalls, 1);
        ret = flextcp_connection_tx_send(&co->context, &c->conn, c->to_send);
        if (ret != 0) {
            STATS_ADD(co, tx_sfail, 1);
            return 1;
        }
        STATS_ADD(co, tx_bytes, c->to_send);
        c->to_send = 0;
    }

    return 0;
}

static void *thread_run(void *arg)
{
    struct core *co = arg;
    int n, i, cn;
    struct flextcp_event *evs, *ev;
    struct connection *c;

    cn = co->cn;
    prepare_core(co);

    evs = calloc(max_events, sizeof(*evs));
    if (evs == NULL) {
        fprintf(stderr, "Allocating event buffer failed\n");
        abort();
    }

    printf("[%d] Starting event loop\n", cn);
    fflush(stdout);
    while (1) {
        if ((n = flextcp_context_poll(&co->context, max_events, evs)) < 0) {
            printf("[%d] flextcp_context_poll failed\n", cn);
            abort();
        }
#ifdef PRINT_STATS
        STATS_ADD(co, poll_hist[n], 1);
#endif

        for (i = 0; i < n; i++) {
            ev = evs + i;

            switch (ev->event_type) {
                case FLEXTCP_EV_LISTEN_OPEN:
                    /* listener is ready now, nothing to do */
                    break;

                case FLEXTCP_EV_LISTEN_NEWCONN:
                    /* new connection arrived, need to accept */
                    accept_connection(co);
                    break;

                case FLEXTCP_EV_LISTEN_ACCEPT:
                    /* accept succeeded */
                    accepted_connection(co, ev);
                    break;

                case FLEXTCP_EV_CONN_RECEIVED:
                    /* received data on connection */
                    c = (struct connection *) ev->ev.conn_received.conn;
                    c->to_alloc += ev->ev.conn_received.len;
                    conn_send(co, c);
                    STATS_ADD(co, rx_events, 1);
                    STATS_ADD(co, rx_bytes, ev->ev.conn_received.len);
                    break;

                case FLEXTCP_EV_CONN_SENDBUF:
                    /* send buffer on connection opened up from 0 */
                    c = (struct connection *) ev->ev.conn_sendbuf.conn;
                    conn_send(co, c);
                    break;

                default:
                    fprintf(stderr, "[%d] Unexpected flextcp event: %u\n", cn,
                            ev->event_type);
            }
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    unsigned num_threads, i;
    struct core *cs;
    pthread_t *pts;
#ifdef PRINT_STATS
    unsigned j;
    uint64_t x;
#endif

    if (argc < 3 || argc > 5) {
        fprintf(stderr, "Usage: ./bench_ll_echo PORT THREADS [MAX-FLOWS] "
            "[MAX-BYTES]\n");
        return EXIT_FAILURE;
    }

    listen_port = atoi(argv[1]);
    num_threads = atoi(argv[2]);
    if (argc >= 3) {
        max_flows = atoi(argv[3]);
    }
    if (argc >= 4) {
        max_bytes = atoi(argv[4]);
    }

    if (flextcp_init() != 0) {
        fprintf(stderr, "flextcp_init failed\n");
        return EXIT_FAILURE;
    }

    pts = calloc(num_threads, sizeof(*pts));
    cs = calloc(num_threads, sizeof(*cs));
    if (pts == NULL || cs == NULL) {
        fprintf(stderr, "allocating thread handles failed\n");
        return EXIT_FAILURE;
    }

    for (i = 0; i < num_threads; i++) {
        cs->cn = i;
        if (flextcp_context_create(&cs->context) != 0) {
            fprintf(stderr, "flextcp_context_create failed %d\n", i);
            return EXIT_FAILURE;
        }
        if (pthread_create(pts + i, NULL, thread_run, cs + i)) {
            fprintf(stderr, "pthread_create failed\n");
            return EXIT_FAILURE;
        }
    }

    sleep(2);
    while (1) {
        sleep(1);
#ifdef PRINT_STATS
        for (i = 0; i < num_threads; i++) {
            uint64_t rx_events = read_cnt(&cs[i].rx_events);
            uint64_t rx_bytes = read_cnt(&cs[i].rx_bytes);
            uint64_t tx_acalls = read_cnt(&cs[i].tx_acalls);
            uint64_t tx_scalls = read_cnt(&cs[i].tx_scalls);
            uint64_t tx_afail = read_cnt(&cs[i].tx_afail);
            uint64_t tx_sfail = read_cnt(&cs[i].tx_sfail);
            uint64_t tx_bytes = read_cnt(&cs[i].tx_bytes);

            printf("    core %2d: (re=%"PRIu64", rb=%"PRIu64 ", tac=%"PRIu64
                    ", tsc=%"PRIu64", taf=%"PRIu64", tsf=%"PRIu64", tb=%"PRIu64
                    ")", i, rx_events, rx_bytes, tx_acalls, tx_scalls, tx_afail,
                    tx_sfail, tx_bytes);
            for (j = 0; j < max_events + 1; j++) {
              if ((x = read_cnt(&cs[i].poll_hist[j])) != 0) {
                printf(" poll[%u]=%"PRIu64, j, x);
              }
            }
            printf("\n");
        }
#endif
    }

    return EXIT_SUCCESS;
}
