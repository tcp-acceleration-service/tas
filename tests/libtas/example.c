#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tas_ll.h>

#include "testutils.h"
#include "harness.h"

#define TEST_IP   0x0a010203
#define TEST_PORT 12345

#define TEST_LIP   0x0a010201
#define TEST_LPORT 23456

static void test_poll_empty(void *p)
{
  struct flextcp_context ctx;
  struct flextcp_event evs[4];
  int num;

  if (flextcp_init() != 0)
    test_error("flextcp_init failed");

  test_randinit(&ctx, sizeof(ctx));
  if (flextcp_context_create(&ctx) != 0)
    test_error("flextcp_context_create failed");

  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success, no events 1", num == 0);

  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success, no events 2", num == 0);
}

static void test_connect_success(void *p)
{
  struct flextcp_context ctx;
  struct flextcp_connection conn;
  struct flextcp_event evs[4];
  int num;
  int n;
  void *rxbuf, *txbuf, *buf;
  ssize_t res;

  if (flextcp_init() != 0)
    test_error("flextcp_init failed");

  test_randinit(&ctx, sizeof(ctx));
  if (flextcp_context_create(&ctx) != 0)
    test_error("flextcp_context_create failed");

  /* initiate connect */
  test_randinit(&conn, sizeof(conn));
  if (flextcp_connection_open(&ctx, &conn, TEST_IP, TEST_PORT) != 0)
    test_error("flextcp_connection_open failed");

  /* check aout entry for new connection request */
  n = harness_aout_pull_connopen(0, (uintptr_t) &conn, TEST_IP, TEST_PORT, 0);
  test_assert("pulling conn open request off aout", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success, no events 1", num == 0);

  /* push ain entry for new connection response */
  rxbuf = test_zalloc(1024);
  txbuf = test_zalloc(1024);
  n = harness_ain_push_connopened(0, (uintptr_t) &conn, 1024, rxbuf, 1024,
      txbuf, 1, TEST_LIP, TEST_LPORT, 0);
  test_assert("harness_ain_push_connopened success", n == 0);

  /* check that there is exactly one event in the context yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success 1 event", num == 1);
  test_assert("ctxev_type", evs[0].event_type == FLEXTCP_EV_CONN_OPEN);
  test_assert("ctxev_status", evs[0].ev.conn_open.status == 0);
  test_assert("ctxev_conn", evs[0].ev.conn_open.conn == &conn);

  /* make sure that allocated tx buffer is available */
  res = flextcp_connection_tx_alloc(&conn, 32, &buf);
  test_assert("tx alloc len", res == 32);
  test_assert("tx alloc buf", buf == txbuf);

  /* add some bytes to receive buffer */
  n = harness_arx_push(0, 0, (uintptr_t) &conn, 32, 0, 0, 0);
  test_assert("harness_arx_push success", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("one rx event poll", num == 1);
  test_assert("rxev_type", evs[0].event_type == FLEXTCP_EV_CONN_RECEIVED);
  test_assert("rxev_buf", evs[0].ev.conn_received.buf == rxbuf);
  test_assert("rxev_len", evs[0].ev.conn_received.len == 32);
  test_assert("rxev_conn", evs[0].ev.conn_received.conn == &conn);
}

static void test_connect_fail(void *p)
{
  struct flextcp_context ctx;
  struct flextcp_connection conn;
  struct flextcp_event evs[4];
  int num;
  int n;

  if (flextcp_init() != 0)
    test_error("flextcp_init failed");

  test_randinit(&ctx, sizeof(ctx));
  if (flextcp_context_create(&ctx) != 0)
    test_error("flextcp_context_create failed");

  /* initiate connect */
  test_randinit(&conn, sizeof(conn));
  if (flextcp_connection_open(&ctx, &conn, TEST_IP, TEST_PORT) != 0)
    test_error("flextcp_connection_open failed");

  /* check aout entry for new connection request */
  n = harness_aout_pull_connopen(0, (uintptr_t) &conn, TEST_IP, TEST_PORT, 0);
  test_assert("pulling conn open request off aout", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success, no events 1", num == 0);

  /* push ain entry for new connection response */
  n = harness_ain_push_connopen_failed(0, (uintptr_t) &conn, 1);
  test_assert("harness_ain_push_connopen_failed success", n == 0);

  /* check that there is exactly one event in the context yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success 1 event", num == 1);
  test_assert("ctxev_type", evs[0].event_type == FLEXTCP_EV_CONN_OPEN);
  test_assert("ctxev_status", evs[0].ev.conn_open.status == 1);
  test_assert("ctxev_conn", evs[0].ev.conn_open.conn == &conn);
}

static void test_full_rxbuf(void *p)
{
  struct flextcp_context ctx;
  struct flextcp_connection conn;
  struct flextcp_event evs[4];
  int num;
  int n;
  void *rxbuf, *txbuf;

  if (flextcp_init() != 0)
    test_error("flextcp_init failed");

  test_randinit(&ctx, sizeof(ctx));
  if (flextcp_context_create(&ctx) != 0)
    test_error("flextcp_context_create failed");

  /* initiate connect */
  test_randinit(&conn, sizeof(conn));
  if (flextcp_connection_open(&ctx, &conn, TEST_IP, TEST_PORT) != 0)
    test_error("flextcp_connection_open failed");

  /* check aout entry for new connection request */
  n = harness_aout_pull_connopen(0, (uintptr_t) &conn, TEST_IP, TEST_PORT, 0);
  test_assert("pulling conn open request off aout", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success, no events 1", num == 0);

  /* push ain entry for new connection response */
  rxbuf = test_zalloc(1024);
  txbuf = test_zalloc(1024);
  n = harness_ain_push_connopened(0, (uintptr_t) &conn, 1024, rxbuf, 1024,
      txbuf, 1, TEST_LIP, TEST_LPORT, 0);
  test_assert("harness_ain_push_connopened success", n == 0);

  /* check that there is exactly one event in the context yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success 1 event", num == 1);
  test_assert("ctxev_type", evs[0].event_type == FLEXTCP_EV_CONN_OPEN);
  test_assert("ctxev_status", evs[0].ev.conn_open.status == 0);
  test_assert("ctxev_conn", evs[0].ev.conn_open.conn == &conn);


  /****************************************/
  /* First message */

  /* add some bytes to receive buffer */
  n = harness_arx_push(0, 0, (uintptr_t) &conn, 1024, 0, 0, 0);
  test_assert("harness_arx_push success", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("one rx event poll", num == 1);
  test_assert("rxev_type", evs[0].event_type == FLEXTCP_EV_CONN_RECEIVED);
  test_assert("rxev_buf", evs[0].ev.conn_received.buf == rxbuf);
  test_assert("rxev_len", evs[0].ev.conn_received.len == 1024);
  test_assert("rxev_conn", evs[0].ev.conn_received.conn == &conn);

  /* free rxbuffer bytes */
  n = flextcp_connection_rx_done(&ctx, &conn, 1024);
  test_assert("flextcp_connection_rx_done success", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success, no events 2", num == 0);

  /* process rx free */
  n = harness_atx_pull(0, 0, 1024, 0, 1, 0, 0);
  test_assert("harness_atx_pull success", n == 0);


  /****************************************/
  /* Second message */

  /* add some bytes to receive buffer */
  n = harness_arx_push(0, 0, (uintptr_t) &conn, 1024, 0, 0, 0);
  test_assert("harness_arx_push success", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("one rx event poll", num == 1);
  test_assert("rxev_type", evs[0].event_type == FLEXTCP_EV_CONN_RECEIVED);
  test_assert("rxev_buf", evs[0].ev.conn_received.buf == rxbuf);
  test_assert("rxev_len", evs[0].ev.conn_received.len == 1024);
  test_assert("rxev_conn", evs[0].ev.conn_received.conn == &conn);

  /* free rxbuffer bytes */
  n = flextcp_connection_rx_done(&ctx, &conn, 1024);
  test_assert("flextcp_connection_rx_done success", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success, no events 2", num == 0);

  /* process rx free */
  n = harness_atx_pull(0, 0, 1024, 0, 1, 1, 0);
  test_assert("harness_atx_pull 2 success", n == 0);
}

static void test_full_txbuf(void *p)
{
  struct flextcp_context ctx;
  struct flextcp_connection conn;
  struct flextcp_event evs[4];
  ssize_t res;
  int num;
  int n;
  void *rxbuf, *txbuf, *buf;

  if (flextcp_init() != 0)
    test_error("flextcp_init failed");

  test_randinit(&ctx, sizeof(ctx));
  if (flextcp_context_create(&ctx) != 0)
    test_error("flextcp_context_create failed");

  /* initiate connect */
  test_randinit(&conn, sizeof(conn));
  if (flextcp_connection_open(&ctx, &conn, TEST_IP, TEST_PORT) != 0)
    test_error("flextcp_connection_open failed");

  /* check aout entry for new connection request */
  n = harness_aout_pull_connopen(0, (uintptr_t) &conn, TEST_IP, TEST_PORT, 0);
  test_assert("pulling conn open request off aout", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success, no events 1", num == 0);

  /* push ain entry for new connection response */
  rxbuf = test_zalloc(1024);
  txbuf = test_zalloc(1024);
  n = harness_ain_push_connopened(0, (uintptr_t) &conn, 1024, rxbuf, 1024,
      txbuf, 1, TEST_LIP, TEST_LPORT, 0);
  test_assert("harness_ain_push_connopened success", n == 0);

  /* check that there is exactly one event in the context yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("success 1 event", num == 1);
  test_assert("ctxev_type", evs[0].event_type == FLEXTCP_EV_CONN_OPEN);
  test_assert("ctxev_status", evs[0].ev.conn_open.status == 0);
  test_assert("ctxev_conn", evs[0].ev.conn_open.conn == &conn);


  /****************************************/
  /* First message */

  /* make sure that allocated tx buffer is available */
  res = flextcp_connection_tx_alloc(&conn, 1024, &buf);
  test_assert("tx alloc len", res == 1024);
  test_assert("tx alloc buf", buf == txbuf);

  n = flextcp_connection_tx_send(&ctx, &conn, 1024);
  test_assert("flextcp_connection_tx_send success", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("one rx event poll", num == 0);

  /* process tx bump */
  n = harness_atx_pull(0, 0, 0, 1024, 1, 0, 0);
  test_assert("harness_atx_pull success", n == 0);

  /* mark bytes as sent */
  n = harness_arx_push(0, 0, (uintptr_t) &conn, 0, 0, 1024, 0);
  test_assert("harness_arx_push success", n == 0);

  /* check that there is a tx buf avail events on the queue*/
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("one rx event poll", num == 1);
  test_assert("txev_type", evs[0].event_type == FLEXTCP_EV_CONN_SENDBUF);
  test_assert("txev_conn", evs[0].ev.conn_sendbuf.conn == &conn);


  /****************************************/
  /* Send message */

  /* make sure that allocated tx buffer is available */
  res = flextcp_connection_tx_alloc(&conn, 1024, &buf);
  test_assert("tx alloc len", res == 1024);
  test_assert("tx alloc buf", buf == txbuf);

  n = flextcp_connection_tx_send(&ctx, &conn, 1024);
  test_assert("flextcp_connection_tx_send success", n == 0);

  /* check that there is no events on the queue yet */
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("one rx event poll", num == 0);

  /* process tx bump */
  n = harness_atx_pull(0, 0, 0, 1024, 1, 1, 0);
  test_assert("harness_atx_pull success", n == 0);

  /* mark bytes as sent */
  n = harness_arx_push(0, 0, (uintptr_t) &conn, 0, 0, 1024, 0);
  test_assert("harness_arx_push success", n == 0);

  /* check that there is a tx buf avail events on the queue*/
  num = flextcp_context_poll(&ctx, 4, evs);
  test_assert("one rx event poll", num == 1);
  test_assert("txev_type", evs[0].event_type == FLEXTCP_EV_CONN_SENDBUF);
  test_assert("txev_conn", evs[0].ev.conn_sendbuf.conn == &conn);
}


int main(int argc, char *argv[])
{
  struct harness_params params;
  params.num_ctxs = 1;
  params.fp_cores = 2;
  params.arx_len = 1024;
  params.atx_len = 1024;
  params.ain_len = 1024;
  params.aout_len = 1024;

  harness_prepare(&params);


  test_subcase("poll empty ctx", test_poll_empty, NULL);
  test_subcase("connect success", test_connect_success, NULL);
  test_subcase("connect fail", test_connect_fail, NULL);
  test_subcase("full rxbuf", test_full_rxbuf, NULL);
  test_subcase("full txbuf", test_full_txbuf, NULL);
  return 0;
}
