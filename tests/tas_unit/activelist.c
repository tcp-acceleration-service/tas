#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../testutils.h"

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#include <tas.h>
#include <tas_memif.h>
#include "../../tas/include/config.h"
#include "../../tas/fast/internal.h"
#include "../../tas/fast/fastemu.h"

/* Redefined so tests compile properly */
/***************************************************************************/
void **vm_shm = (void *) 0;
struct flextcp_pl_mem state_base;
struct flextcp_pl_mem *fp_state = &state_base;
struct configuration config;

int fast_flows_bump(struct dataplane_context *ctx, uint32_t flow_id,
        uint16_t bump_seq, uint32_t rx_bump, uint32_t tx_bump, uint8_t flags,
        struct network_buf_handle *nbh, uint32_t ts) 
{
    return 0;
}

static void polled_vm_init(struct polled_vm *vm, uint16_t id)
{
  vm->id = id;
  vm->next = IDXLIST_INVAL;
  vm->prev = IDXLIST_INVAL;
  vm->flags = 0;
  vm->poll_next_ctx = 0;
  vm->act_ctx_head = IDXLIST_INVAL;
  vm->act_ctx_tail = IDXLIST_INVAL;
}

static void polled_ctx_init(struct polled_context *ctx, uint32_t id, uint32_t vmid)
{
  ctx->id = id;
  ctx->vmid = vmid;
  ctx->next = IDXLIST_INVAL;
  ctx->prev = IDXLIST_INVAL;
  ctx->flags = 0;
  ctx->null_rounds = 0;
}
/***************************************************************************/


void test_enqueue_vm(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_vm_to_active(ctx, 0);
    test_assert("vm 0  is head", ctx->act_head == 0);
    test_assert("vm 0  is tail", ctx->act_tail == 0);
    test_assert("vm 0 is active", 
            (ctx->polled_vms[0].flags & FLAG_ACTIVE) != 0);
    enqueue_vm_to_active(ctx, 1);
    test_assert("vm 0  is head", ctx->act_head == 0);
    test_assert("vm 1  is tail", ctx->act_tail == 1);
    test_assert("vm 1 is active", 
            (ctx->polled_vms[1].flags & FLAG_ACTIVE) != 0);
    enqueue_vm_to_active(ctx, 2);
    test_assert("vm 0  is head", ctx->act_head == 0);
    test_assert("vm 2  is tail", ctx->act_tail == 2);
    test_assert("vm 2 is active", 
            (ctx->polled_vms[2].flags & FLAG_ACTIVE) != 0);
}

void test_enqueue_ctx(void *arg)
{
    struct polled_vm *vm0, *vm1;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    vm0 = &ctx->polled_vms[0];
    vm1 = &ctx->polled_vms[1];
    
    polled_vm_init(vm0, 0);
    polled_ctx_init(&vm0->ctxs[0], 0, 0);
    polled_ctx_init(&vm0->ctxs[1], 1, 0);

    polled_vm_init(vm1, 1);
    polled_ctx_init(&vm0->ctxs[0], 0, 1);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(vm0, 0);
    test_assert("vm0: ctx 0 is head", vm0->act_ctx_head == 0);
    test_assert("vm0: ctx 0 is tail", vm0->act_ctx_tail == 0);
    test_assert("vm0: ctx 0 is active", 
            (vm0->ctxs[0].flags & FLAG_ACTIVE) != 0);
    enqueue_ctx_to_active(vm0, 1);
    test_assert("vm0: ctx 0 is head", vm0->act_ctx_head == 0);
    test_assert("vm0: ctx 1 is tail", vm0->act_ctx_tail == 1);
    test_assert("vm0: ctx 1 is active", 
            (vm0->ctxs[1].flags & FLAG_ACTIVE) != 0);
    enqueue_ctx_to_active(vm1, 0);
    test_assert("vm1: ctx 0 is head", vm1->act_ctx_head == 0);
    test_assert("vm1: ctx 0 is tail", vm1->act_ctx_tail == 0);
    test_assert("vm1: ctx 0 is active", 
            (vm1->ctxs[0].flags & FLAG_ACTIVE) != 0);
}

void test_remove_vm(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_vm_to_active(ctx, 0);
    enqueue_vm_to_active(ctx, 1);
    enqueue_vm_to_active(ctx, 2);

    remove_vm_from_active(ctx, &ctx->polled_vms[1]);
    test_assert("vm 0 is head", ctx->act_head == 0);
    test_assert("vm 2 is tail", ctx->act_tail == 2);
    test_assert("vm 1 is not active", 
            (ctx->polled_vms[1].flags & FLAG_ACTIVE) == 0);
}

void test_remove_vm_head(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_vm_to_active(ctx, 0);
    enqueue_vm_to_active(ctx, 1);
    enqueue_vm_to_active(ctx, 2);

    remove_vm_from_active(ctx, &ctx->polled_vms[0]);
    test_assert("vm 1 is head", ctx->act_head == 1);
    test_assert("vm 2 is tail", ctx->act_tail == 2);
    test_assert("vm 0 is not active", 
            (ctx->polled_vms[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_vm_tail(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_vm_to_active(ctx, 0);
    enqueue_vm_to_active(ctx, 1);
    enqueue_vm_to_active(ctx, 2);

    remove_vm_from_active(ctx, &ctx->polled_vms[2]);
    test_assert("vm 0 is head", ctx->act_head == 0);
    test_assert("vm 1 is tail", ctx->act_tail == 1);
    test_assert("vm 2 is not active", 
            (ctx->polled_vms[2].flags & FLAG_ACTIVE) == 0);
}

void test_remove_vm_one_elt(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_vm_to_active(ctx, 0);

    remove_vm_from_active(ctx, &ctx->polled_vms[0]);
    test_assert("head is invalid", ctx->act_head == IDXLIST_INVAL);
    test_assert("tail is invalid", ctx->act_tail == IDXLIST_INVAL);
    test_assert("vm 0 is not active", 
            (ctx->polled_vms[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_multiple_vms(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_vm_to_active(ctx, 0);
    enqueue_vm_to_active(ctx, 1);
    enqueue_vm_to_active(ctx, 2);

    remove_vm_from_active(ctx, &ctx->polled_vms[1]);
    test_assert("vm 0 is head", ctx->act_head == 0);
    test_assert("vm 2 is tail", ctx->act_tail == 2);
    test_assert("vm 1 is not active", 
            (ctx->polled_vms[1].flags & FLAG_ACTIVE) == 0);

    remove_vm_from_active(ctx, &ctx->polled_vms[2]);
    test_assert("vm 0 is head", ctx->act_head == 0);
    test_assert("vm 0 is tail", ctx->act_tail == 0);
    test_assert("vm 2 is not active", 
            (ctx->polled_vms[1].flags & FLAG_ACTIVE) == 0);

    remove_vm_from_active(ctx, &ctx->polled_vms[0]);
    test_assert("head is invalid", ctx->act_head == IDXLIST_INVAL);
    test_assert("tail is invalid", ctx->act_tail == IDXLIST_INVAL);
    test_assert("vm 0 is not active", 
            (ctx->polled_vms[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_ctx(void *arg)
{
    struct polled_vm *vm0;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    vm0 = &ctx->polled_vms[0];
    
    polled_vm_init(vm0, 0);
    polled_ctx_init(&vm0->ctxs[0], 0, 0);
    polled_ctx_init(&vm0->ctxs[1], 1, 0);
    polled_ctx_init(&vm0->ctxs[2], 2, 0);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(vm0, 0);
    enqueue_ctx_to_active(vm0, 1);
    enqueue_ctx_to_active(vm0, 2);

    remove_ctx_from_active(vm0, &vm0->ctxs[1]);
    test_assert("ctx0 is head", vm0->act_ctx_head == 0);
    test_assert("ctx2 is tail", vm0->act_ctx_tail == 2);
    test_assert("ctx1 is not active", 
            (vm0->ctxs[1].flags & FLAG_ACTIVE) == 0);
}

void test_remove_ctx_head(void *arg)
{
    struct polled_vm *vm0;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    vm0 = &ctx->polled_vms[0];
    
    polled_vm_init(vm0, 0);
    polled_ctx_init(&vm0->ctxs[0], 0, 0);
    polled_ctx_init(&vm0->ctxs[1], 1, 0);
    polled_ctx_init(&vm0->ctxs[2], 2, 0);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(vm0, 0);
    enqueue_ctx_to_active(vm0, 1);
    enqueue_ctx_to_active(vm0, 2);

    remove_ctx_from_active(vm0, &vm0->ctxs[0]);
    test_assert("ctx1 is head", vm0->act_ctx_head == 1);
    test_assert("ctx2 is tail", vm0->act_ctx_tail == 2);
    test_assert("ctx0 is not active", 
            (vm0->ctxs[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_ctx_tail(void *arg)
{
    struct polled_vm *vm0;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    vm0 = &ctx->polled_vms[0];
    
    polled_vm_init(vm0, 0);
    polled_ctx_init(&vm0->ctxs[0], 0, 0);
    polled_ctx_init(&vm0->ctxs[1], 1, 0);
    polled_ctx_init(&vm0->ctxs[2], 2, 0);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(vm0, 0);
    enqueue_ctx_to_active(vm0, 1);
    enqueue_ctx_to_active(vm0, 2);

    remove_ctx_from_active(vm0, &vm0->ctxs[2]);
    test_assert("ctx0 is head", vm0->act_ctx_head == 0);
    test_assert("ctx1 is tail", vm0->act_ctx_tail == 1);
    test_assert("ctx2 is not active", 
            (vm0->ctxs[2].flags & FLAG_ACTIVE) == 0);
}

void test_remove_ctx_one_elt(void *arg)
{
    struct polled_vm *vm0;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    vm0 = &ctx->polled_vms[0];
    
    polled_vm_init(vm0, 0);
    polled_ctx_init(&vm0->ctxs[0], 0, 0);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(vm0, 0);

    remove_ctx_from_active(vm0, &vm0->ctxs[0]);
    test_assert("head is invalid", vm0->act_ctx_head == IDXLIST_INVAL);
    test_assert("tail is invalid", vm0->act_ctx_tail == IDXLIST_INVAL);
    test_assert("ctx0 is not active", 
            (vm0->ctxs[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_multiple_ctx(void *arg)
{
    struct polled_vm *vm0, *vm1;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    vm0 = &ctx->polled_vms[0];
    vm1 = &ctx->polled_vms[1];
    
    polled_vm_init(vm0, 0);
    polled_ctx_init(&vm0->ctxs[0], 0, 0);
    polled_ctx_init(&vm0->ctxs[1], 1, 0);
    polled_ctx_init(&vm0->ctxs[2], 2, 0);

    polled_vm_init(vm1, 1);
    polled_ctx_init(&vm1->ctxs[0], 0, 1);
    polled_ctx_init(&vm1->ctxs[1], 1, 1);
    polled_ctx_init(&vm1->ctxs[2], 2, 1);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(vm0, 0);
    enqueue_ctx_to_active(vm0, 1);
    enqueue_ctx_to_active(vm0, 2);

    enqueue_ctx_to_active(vm1, 0);
    enqueue_ctx_to_active(vm1, 1);
    enqueue_ctx_to_active(vm1, 2);

    /* Remove contexts from vm 0 */
    remove_ctx_from_active(vm0, &vm0->ctxs[1]);
    test_assert("vm0: ctx0 is head", vm0->act_ctx_head == 0);
    test_assert("vm0: ctx2 is tail", vm0->act_ctx_tail == 2);
    test_assert("vm0: ctx1 is not active", 
            (vm0->ctxs[1].flags & FLAG_ACTIVE) == 0);

    remove_ctx_from_active(vm0, &vm0->ctxs[2]);
    test_assert("vm0: ctx0 is head", vm0->act_ctx_head == 0);
    test_assert("vm0: ctx0 is tail", vm0->act_ctx_tail == 0);
    test_assert("vm0: ctx2 is not active", 
            (vm0->ctxs[2].flags & FLAG_ACTIVE) == 0);

    remove_ctx_from_active(vm0, &vm0->ctxs[0]);
    test_assert("vm0: head is invalid", vm0->act_ctx_head == IDXLIST_INVAL);
    test_assert("vm0: tail is invalid", vm0->act_ctx_tail == IDXLIST_INVAL);
    test_assert("vm0: ctx0 is not active", 
            (vm0->ctxs[0].flags & FLAG_ACTIVE) == 0);

    /* Remove contexts from vm 1 */
    remove_ctx_from_active(vm1, &vm1->ctxs[1]);
    test_assert("vm1: ctx0 is head", vm1->act_ctx_head == 0);
    test_assert("vm1: ctx2 is tail", vm1->act_ctx_tail == 2);
    test_assert("vm1: ctx1 is not active", 
            (vm1->ctxs[1].flags & FLAG_ACTIVE) == 0);

    remove_ctx_from_active(vm1, &vm1->ctxs[2]);
    test_assert("vm1: ctx0 is head", vm1->act_ctx_head == 0);
    test_assert("vm1: ctx0 is tail", vm1->act_ctx_tail == 0);
    test_assert("vm1: ctx2 is not active", 
            (vm1->ctxs[2].flags & FLAG_ACTIVE) == 0);

    remove_ctx_from_active(vm1, &vm1->ctxs[0]);
    test_assert("vm1: head is invalid", vm1->act_ctx_head == IDXLIST_INVAL);
    test_assert("vm1: tail is invalid", vm1->act_ctx_tail == IDXLIST_INVAL);
    test_assert("vm1: ctx0 is not active", 
            (vm1->ctxs[0].flags & FLAG_ACTIVE) == 0);
}

int main(int argc, char *argv[])
{
    int ret = 0;

    if (test_subcase("enqueue vm", test_enqueue_vm, NULL))
        ret = 1;
    
    if (test_subcase("enqueue ctx", test_enqueue_ctx, NULL))
        ret = 1;

    if (test_subcase("remove vm", test_remove_vm, NULL))
        ret = 1;

    if (test_subcase("remove vm head", test_remove_vm_head, NULL))
        ret = 1;

    if (test_subcase("remove vm tail", test_remove_vm_tail, NULL))
        ret = 1;

    if (test_subcase("remove vm one elt", test_remove_vm_one_elt, NULL))
        ret = 1;

    if (test_subcase("remove mult vm", test_remove_multiple_vms, NULL))
        ret = 1;

    if (test_subcase("remove ctx", test_remove_ctx, NULL))
        ret = 1;

    if (test_subcase("remove ctx head", test_remove_ctx_head, NULL))
        ret = 1;

    if (test_subcase("remove ctx tail", test_remove_ctx_tail, NULL))
        ret = 1;

    if (test_subcase("remove ctx one elt", test_remove_ctx_one_elt, NULL))
        ret = 1;

    if (test_subcase("remove mult ctx", test_remove_multiple_ctx, NULL))
        ret = 1;


    return ret;
}