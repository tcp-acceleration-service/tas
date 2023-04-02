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

#define TEST_TCP_MSS 64
#define TEST_BATCH_SIZE 4


void test_qman_rr_base(void *arg) 
{
  unsigned num;
  struct qman_thread *t;
  uint32_t avail;
  int all_vm1, all_vm2, ret;
  struct dataplane_context *ctx;
  uint16_t q_bytes[TEST_BATCH_SIZE];
  unsigned vm_ids[TEST_BATCH_SIZE], q_ids[TEST_BATCH_SIZE];

  ret = rte_eal_init(3, arg);
  test_assert("rte_eal_init", ret > -1);

  // Allocate memory for one context in 1 core
  ctx = rte_calloc("context", 1, sizeof(ctx), 0);

  ret = tas_qman_thread_init(ctx);
  test_assert("init qman thread", ret > -1);

  t = &ctx->qman;

  // Set 1 packet for 4 flows in vm 1
  tas_qman_set(t, 1, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 3, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_set set vm 1 check vm 1", avail == 64 * 4);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_set set vm 1 check vm 2", avail == 0);

  // Set 1 packet for 4 flows in vm 2
  tas_qman_set(t, 2, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 3, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_set set vm 2 check vm 1", avail == 64 * 4);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_set set vm 2 check vm 2", avail == 64 * 4);

  num = TEST_BATCH_SIZE;
  tas_qman_poll(ctx, num, vm_ids, q_ids, q_bytes);
  all_vm1 = (vm_ids[0] == 1) && (vm_ids[1] == 1) 
      && (vm_ids[2] == 1) && (vm_ids[3] == 1);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_poll poll vm 1 check avail vm 1", avail == 0);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_poll poll vm 1 check avail vm 2", avail == 64 * 4);
  test_assert("scheduled all packets from vm 1", all_vm1);

  memset(vm_ids, 0, sizeof(*vm_ids) * TEST_BATCH_SIZE);
  memset(q_ids, 0, sizeof(*q_ids) * TEST_BATCH_SIZE);
  memset(q_bytes, 0, sizeof(*q_bytes) * TEST_BATCH_SIZE);

  num = TEST_BATCH_SIZE;
  tas_qman_poll(ctx, num, vm_ids, q_ids, q_bytes);
  all_vm2 = (vm_ids[0] == 2) && (vm_ids[1] == 2) 
      && (vm_ids[2] == 2) && (vm_ids[3] == 2);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_poll poll vm 2 check avail vm 1", avail == 0);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_poll poll vm 2 check avail vm 2", avail == 0);
  test_assert("scheduled all packets from vm 2", all_vm2);

  qman_free_vm_cont(ctx);
  rte_free(ctx);
}

void test_qman_rr_full_loop(void *arg)
{
  struct qman_thread *t;
  uint32_t avail;
  int all_vm1, all_vm2, ret;
  struct dataplane_context *ctx;
  uint16_t q_bytes[TEST_BATCH_SIZE];
  unsigned vm_ids[TEST_BATCH_SIZE], q_ids[TEST_BATCH_SIZE];
  unsigned num = TEST_BATCH_SIZE;

  ret = rte_eal_init(3, arg);
  test_assert("rte_eal_init", ret > -1);

  // Allocate memory for one context in 1 core
  ctx = rte_calloc("context", 1, sizeof(ctx), 0);

  ret = tas_qman_thread_init(ctx);
  test_assert("init qman thread", ret > -1);

  t = &ctx->qman;

  // Set 8 packets in vm 1
  tas_qman_set(t, 1, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 3, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 3, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_set set vm 1 check vm 1", avail == 64 * 8);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_set set vm 1 check vm 2", avail == 0);

  // Set 8 packets in vm 2
  tas_qman_set(t, 2, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 3, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 5, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_set set vm 2 check vm 1", avail == 64 * 8);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_set set vm 2 check vm 2", avail == 64 * 8);
  
  // Poll and expect vm 1
  tas_qman_poll(ctx, num, vm_ids, q_ids, q_bytes);
  all_vm1 = (vm_ids[0] == 1) && (vm_ids[1] == 1) 
      && (vm_ids[2] == 1) && (vm_ids[3] == 1);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_poll (1) poll vm 1 check avail vm 1", avail == 64 * 4);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_poll (1) poll vm 1 check avail vm 2", avail == 64 * 8);
  test_assert("scheduled all packets from vm 1", all_vm1);

  memset(vm_ids, 0, sizeof(*vm_ids) * TEST_BATCH_SIZE);
  memset(q_ids, 0, sizeof(*q_ids) * TEST_BATCH_SIZE);
  memset(q_bytes, 0, sizeof(*q_bytes) * TEST_BATCH_SIZE);

  // Poll and expect vm 2
  tas_qman_poll(ctx, num, vm_ids, q_ids, q_bytes);
  all_vm2 = (vm_ids[0] == 2) && (vm_ids[1] == 2) 
      && (vm_ids[2] == 2) && (vm_ids[3] == 2);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_poll (2) poll vm 2 check avail vm 1", avail == 64 * 4);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_poll (2) poll vm 2 check avail vm 2", avail == 64 * 4);
  test_assert("scheduled all packets from vm 2", all_vm2);

  memset(vm_ids, 0, sizeof(*vm_ids) * TEST_BATCH_SIZE);
  memset(q_ids, 0, sizeof(*q_ids) * TEST_BATCH_SIZE);
  memset(q_bytes, 0, sizeof(*q_bytes) * TEST_BATCH_SIZE);

  // Poll and expect vm 1 again
  tas_qman_poll(ctx, num, vm_ids, q_ids, q_bytes);
  all_vm1 = (vm_ids[0] == 1) && (vm_ids[1] == 1) 
      && (vm_ids[2] == 1) && (vm_ids[3] == 1);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_poll (3) poll vm 1 check avail vm 1", avail == 0);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_poll (3) poll vm 1 check avail vm 2", avail == 64 * 4);
  test_assert("scheduled all packets from vm 1", all_vm1);

  memset(vm_ids, 0, sizeof(*vm_ids) * TEST_BATCH_SIZE);
  memset(q_ids, 0, sizeof(*q_ids) * TEST_BATCH_SIZE);
  memset(q_bytes, 0, sizeof(*q_bytes) * TEST_BATCH_SIZE);

  // Poll and expect vm 2 again
  tas_qman_poll(ctx, num, vm_ids, q_ids, q_bytes);
  all_vm2 = (vm_ids[0] == 2) && (vm_ids[1] == 2) 
      && (vm_ids[2] == 2) && (vm_ids[3] == 2);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_poll (4) poll vm 2 check avail vm 1", avail == 0);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_poll (4) poll vm 2 check avail vm 2", avail == 0);
  test_assert("scheduled all packets from vm 2", all_vm2);

  qman_free_vm_cont(ctx);
  rte_free(ctx);
}

void test_qman_rr_mix_round(void *arg) {
  struct qman_thread *t;
  uint32_t avail;
  int packs_exp, ret;
  struct dataplane_context *ctx;
  uint16_t q_bytes[TEST_BATCH_SIZE];
  unsigned vm_ids[TEST_BATCH_SIZE], q_ids[TEST_BATCH_SIZE];
  unsigned num = TEST_BATCH_SIZE;

  ret = rte_eal_init(3, arg);
  test_assert("rte_eal_init", ret > -1);

  // Allocate memory for one context in 1 core
  ctx = rte_calloc("context", 1, sizeof(ctx), 0);

  ret = tas_qman_thread_init(ctx);
  test_assert("init qman thread", ret > -1);

  t = &ctx->qman;

  // Set 8 packets in vm 1
  tas_qman_set(t, 1, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 1, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);

  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_set set vm 1 check vm 1", avail == 64 * 3);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_set set vm 1 check vm 2", avail == 0);

  // Set 8 packets in vm 2
  tas_qman_set(t, 2, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 1, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 2, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 3, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);
  tas_qman_set(t, 2, 0, 0, 64, TEST_TCP_MSS, QMAN_ADD_AVAIL | QMAN_SET_MAXCHUNK);

  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_set set vm 2 check vm 1", avail == 64 * 3);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_set set vm 2 check vm 2", avail == 64 * 6);
  
  // Poll and expect vm 1 and 2
  tas_qman_poll(ctx, num, vm_ids, q_ids, q_bytes);
  packs_exp = (vm_ids[0] == 1) && (vm_ids[1] == 1) 
      && (vm_ids[2] == 1) && (vm_ids[3] == 2);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_poll (1) poll vm 1 check avail vm 1", avail == 0);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_poll (1) poll vm 1 check avail vm 2", avail == 64 * 5);
  test_assert("qman poll (1) check correct packets scheduled", packs_exp);

  memset(vm_ids, 0, sizeof(*vm_ids) * TEST_BATCH_SIZE);
  memset(q_ids, 0, sizeof(*q_ids) * TEST_BATCH_SIZE);
  memset(q_bytes, 0, sizeof(*q_bytes) * TEST_BATCH_SIZE);

  // Poll and expect vm 2
  tas_qman_poll(ctx, num, vm_ids, q_ids, q_bytes);
  packs_exp = (vm_ids[0] == 2) && (vm_ids[1] == 2) 
      && (vm_ids[2] == 2) && (vm_ids[3] == 2);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_poll (2) poll vm 2 check avail vm 1", avail == 0);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_poll (2) poll vm 2 check avail vm 2", avail == 64);
  test_assert("qman poll (2) check correct packets scheduled", packs_exp);

  memset(vm_ids, 0, sizeof(*vm_ids) * TEST_BATCH_SIZE);
  memset(q_ids, 0, sizeof(*q_ids) * TEST_BATCH_SIZE);
  memset(q_bytes, 0, sizeof(*q_bytes) * TEST_BATCH_SIZE);

  // Poll and expect vm 2 again
  tas_qman_poll(ctx, num, vm_ids, q_ids, q_bytes);
  packs_exp = (vm_ids[0] == 2);
  avail = qman_vm_get_avail(ctx, 1);
  test_assert("qman_poll (3) poll vm 1 check avail vm 1", avail == 0);
  avail = qman_vm_get_avail(ctx, 2);
  test_assert("qman_poll (3) poll vm 1 check avail vm 2", avail == 0);
  test_assert("qman poll (3) check correct packets scheduled", packs_exp);

  qman_free_vm_cont(ctx);
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

  if (test_subcase("test_qman_rr_base", test_qman_rr_base, dpdk_args))
  {
    ret = 1;
  }

  if (test_subcase("test_qman_rr_full_loop", test_qman_rr_full_loop,    
      dpdk_args))
  {
    ret = 1;
  }

  if (test_subcase("test_qman_rr_mix_round", test_qman_rr_mix_round,    
      dpdk_args))
  {
    ret = 1;
  }

  return ret;
}