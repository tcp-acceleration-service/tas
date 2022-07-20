#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rte_malloc.h>

#include <tas.h>
#include <fastpath.h>
#include <utils_rng.h>

#include "../testutils.h"
#include "../tas/fast/internal.h"
#include "../include/tas_memif.h"

#define TEST_TCP_MSS 1460
#define TEST_BATCH_SIZE 4


void test_batch_size_rr(void *arg) 
{
  struct dataplane_context *ctx;
  unsigned num;
  struct qman_thread *t;
  int all_app1, all_app2, ret;
  uint16_t q_bytes[TEST_BATCH_SIZE];
  unsigned app_ids[TEST_BATCH_SIZE], q_ids[TEST_BATCH_SIZE];

  ret = rte_eal_init(3, arg);
  test_assert("rte_eal_init", ret > -1);

  // Allocate memory for one context in 1 core
  ctx = rte_calloc("context", 1, sizeof(ctx), 0);

  ret = qman_thread_init(ctx);
  test_assert("init qman thread", ret > -1);

  t = &ctx->qman;

  // Set 1 packet for 4 flows in app 1
  qman_set(t, 1, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL);
  qman_set(t, 1, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL);
  qman_set(t, 1, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL);
  qman_set(t, 1, 3, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL);

  // Set 1 packet for 4 flows in app 2
  qman_set(t, 2, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL);
  qman_set(t, 2, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL);
  qman_set(t, 2, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL);
  qman_set(t, 2, 3, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL);

  num = TEST_BATCH_SIZE;
  qman_poll(t, num, app_ids, q_ids, q_bytes);
  all_app1 = (app_ids[0] == 1) && (app_ids[1] == 1) 
      && (app_ids[2] == 1) && (app_ids[3] == 1);
  test_assert("scheduled all packets from app 1", all_app1);

  memset(app_ids, 0, sizeof(*app_ids) * TEST_BATCH_SIZE);
  memset(q_ids, 0, sizeof(*q_ids) * TEST_BATCH_SIZE);
  memset(q_bytes, 0, sizeof(*q_bytes) * TEST_BATCH_SIZE);

  num = TEST_BATCH_SIZE;
  qman_poll(t, num, app_ids, q_ids, q_bytes);
  all_app2 = (app_ids[0] == 2) && (app_ids[1] == 2) 
      && (app_ids[2] == 2) && (app_ids[3] == 2);
  test_assert("scheduled all packets from app 2", all_app2);

  free_app_cont(ctx);
  rte_free(ctx);
}

int main(int argc, char *argv[])
{
  int ret;
  char *dpdk_args[3];

  // Create dpdk args to disable eal logging
  dpdk_args[0] = argv[0];
  dpdk_args[1] = "--log-level";
  dpdk_args[2] = "lib.eal:error";

  if (test_subcase("batch size rr", test_batch_size_rr, dpdk_args))
  {
    ret = 1;
  }

  return ret;
}