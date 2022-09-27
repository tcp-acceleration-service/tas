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
void *tas_shm = (void *) 0;
struct flextcp_pl_mem state_base;
struct flextcp_pl_mem *fp_state = &state_base;
struct configuration config;

int fast_flows_bump(struct dataplane_context *ctx, uint32_t flow_id,
        uint16_t bump_seq, uint32_t rx_bump, uint32_t tx_bump, uint8_t flags,
        struct network_buf_handle *nbh, uint32_t ts) 
{
    return 0;
}

static void polled_app_init(struct polled_app *app, uint16_t id)
{
  app->id = id;
  app->next = IDXLIST_INVAL;
  app->prev = IDXLIST_INVAL;
  app->flags = 0;
  app->poll_next_ctx = 0;
  app->act_ctx_head = IDXLIST_INVAL;
  app->act_ctx_tail = IDXLIST_INVAL;
}

static void polled_ctx_init(struct polled_context *ctx, uint32_t id, uint32_t aid)
{
  ctx->id = id;
  ctx->aid = aid;
  ctx->next = IDXLIST_INVAL;
  ctx->prev = IDXLIST_INVAL;
  ctx->flags = 0;
  ctx->null_rounds = 0;
}
/***************************************************************************/


void test_enqueue_app(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_app_to_active(ctx, 0);
    test_assert("app 0  is head", ctx->act_head == 0);
    test_assert("app 0  is tail", ctx->act_tail == 0);
    test_assert("app 0 is active", 
            (ctx->polled_apps[0].flags & FLAG_ACTIVE) != 0);
    enqueue_app_to_active(ctx, 1);
    test_assert("app 0  is head", ctx->act_head == 0);
    test_assert("app 1  is tail", ctx->act_tail == 1);
    test_assert("app 1 is active", 
            (ctx->polled_apps[1].flags & FLAG_ACTIVE) != 0);
    enqueue_app_to_active(ctx, 2);
    test_assert("app 0  is head", ctx->act_head == 0);
    test_assert("app 2  is tail", ctx->act_tail == 2);
    test_assert("app 2 is active", 
            (ctx->polled_apps[2].flags & FLAG_ACTIVE) != 0);
}

void test_enqueue_ctx(void *arg)
{
    struct polled_app *app0, *app1;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    app0 = &ctx->polled_apps[0];
    app1 = &ctx->polled_apps[1];
    
    polled_app_init(app0, 0);
    polled_ctx_init(&app0->ctxs[0], 0, 0);
    polled_ctx_init(&app0->ctxs[1], 1, 0);

    polled_app_init(app1, 1);
    polled_ctx_init(&app0->ctxs[0], 0, 1);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(app0, 0);
    test_assert("app0: ctx 0 is head", app0->act_ctx_head == 0);
    test_assert("app0: ctx 0 is tail", app0->act_ctx_tail == 0);
    test_assert("app0: ctx 0 is active", 
            (app0->ctxs[0].flags & FLAG_ACTIVE) != 0);
    enqueue_ctx_to_active(app0, 1);
    test_assert("app0: ctx 0 is head", app0->act_ctx_head == 0);
    test_assert("app0: ctx 1 is tail", app0->act_ctx_tail == 1);
    test_assert("app0: ctx 1 is active", 
            (app0->ctxs[1].flags & FLAG_ACTIVE) != 0);
    enqueue_ctx_to_active(app1, 0);
    test_assert("app1: ctx 0 is head", app1->act_ctx_head == 0);
    test_assert("app1: ctx 0 is tail", app1->act_ctx_tail == 0);
    test_assert("app1: ctx 0 is active", 
            (app1->ctxs[0].flags & FLAG_ACTIVE) != 0);
}

void test_remove_app(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_app_to_active(ctx, 0);
    enqueue_app_to_active(ctx, 1);
    enqueue_app_to_active(ctx, 2);

    remove_app_from_active(ctx, &ctx->polled_apps[1]);
    test_assert("app 0 is head", ctx->act_head == 0);
    test_assert("app 2 is tail", ctx->act_tail == 2);
    test_assert("app 1 is not active", 
            (ctx->polled_apps[1].flags & FLAG_ACTIVE) == 0);
}

void test_remove_app_head(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_app_to_active(ctx, 0);
    enqueue_app_to_active(ctx, 1);
    enqueue_app_to_active(ctx, 2);

    remove_app_from_active(ctx, &ctx->polled_apps[0]);
    test_assert("app 1 is head", ctx->act_head == 1);
    test_assert("app 2 is tail", ctx->act_tail == 2);
    test_assert("app 0 is not active", 
            (ctx->polled_apps[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_app_tail(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_app_to_active(ctx, 0);
    enqueue_app_to_active(ctx, 1);
    enqueue_app_to_active(ctx, 2);

    remove_app_from_active(ctx, &ctx->polled_apps[2]);
    test_assert("app 0 is head", ctx->act_head == 0);
    test_assert("app 1 is tail", ctx->act_tail == 1);
    test_assert("app 2 is not active", 
            (ctx->polled_apps[2].flags & FLAG_ACTIVE) == 0);
}

void test_remove_app_one_elt(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_app_to_active(ctx, 0);

    remove_app_from_active(ctx, &ctx->polled_apps[0]);
    test_assert("head is invalid", ctx->act_head == IDXLIST_INVAL);
    test_assert("tail is invalid", ctx->act_tail == IDXLIST_INVAL);
    test_assert("app 0 is not active", 
            (ctx->polled_apps[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_multiple_apps(void *arg)
{
    struct dataplane_context *ctx;
    
    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;
    enqueue_app_to_active(ctx, 0);
    enqueue_app_to_active(ctx, 1);
    enqueue_app_to_active(ctx, 2);

    remove_app_from_active(ctx, &ctx->polled_apps[1]);
    test_assert("app 0 is head", ctx->act_head == 0);
    test_assert("app 2 is tail", ctx->act_tail == 2);
    test_assert("app 1 is not active", 
            (ctx->polled_apps[1].flags & FLAG_ACTIVE) == 0);

    remove_app_from_active(ctx, &ctx->polled_apps[2]);
    test_assert("app 0 is head", ctx->act_head == 0);
    test_assert("app 0 is tail", ctx->act_tail == 0);
    test_assert("app 2 is not active", 
            (ctx->polled_apps[1].flags & FLAG_ACTIVE) == 0);

    remove_app_from_active(ctx, &ctx->polled_apps[0]);
    test_assert("head is invalid", ctx->act_head == IDXLIST_INVAL);
    test_assert("tail is invalid", ctx->act_tail == IDXLIST_INVAL);
    test_assert("app 0 is not active", 
            (ctx->polled_apps[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_ctx(void *arg)
{
    struct polled_app *app0;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    app0 = &ctx->polled_apps[0];
    
    polled_app_init(app0, 0);
    polled_ctx_init(&app0->ctxs[0], 0, 0);
    polled_ctx_init(&app0->ctxs[1], 1, 0);
    polled_ctx_init(&app0->ctxs[2], 2, 0);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(app0, 0);
    enqueue_ctx_to_active(app0, 1);
    enqueue_ctx_to_active(app0, 2);

    remove_ctx_from_active(app0, &app0->ctxs[1]);
    test_assert("ctx0 is head", app0->act_ctx_head == 0);
    test_assert("ctx2 is tail", app0->act_ctx_tail == 2);
    test_assert("ctx1 is not active", 
            (app0->ctxs[1].flags & FLAG_ACTIVE) == 0);
}

void test_remove_ctx_head(void *arg)
{
    struct polled_app *app0;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    app0 = &ctx->polled_apps[0];
    
    polled_app_init(app0, 0);
    polled_ctx_init(&app0->ctxs[0], 0, 0);
    polled_ctx_init(&app0->ctxs[1], 1, 0);
    polled_ctx_init(&app0->ctxs[2], 2, 0);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(app0, 0);
    enqueue_ctx_to_active(app0, 1);
    enqueue_ctx_to_active(app0, 2);

    remove_ctx_from_active(app0, &app0->ctxs[0]);
    test_assert("ctx1 is head", app0->act_ctx_head == 1);
    test_assert("ctx2 is tail", app0->act_ctx_tail == 2);
    test_assert("ctx0 is not active", 
            (app0->ctxs[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_ctx_tail(void *arg)
{
    struct polled_app *app0;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    app0 = &ctx->polled_apps[0];
    
    polled_app_init(app0, 0);
    polled_ctx_init(&app0->ctxs[0], 0, 0);
    polled_ctx_init(&app0->ctxs[1], 1, 0);
    polled_ctx_init(&app0->ctxs[2], 2, 0);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(app0, 0);
    enqueue_ctx_to_active(app0, 1);
    enqueue_ctx_to_active(app0, 2);

    remove_ctx_from_active(app0, &app0->ctxs[2]);
    test_assert("ctx0 is head", app0->act_ctx_head == 0);
    test_assert("ctx1 is tail", app0->act_ctx_tail == 1);
    test_assert("ctx2 is not active", 
            (app0->ctxs[2].flags & FLAG_ACTIVE) == 0);
}

void test_remove_ctx_one_elt(void *arg)
{
    struct polled_app *app0;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    app0 = &ctx->polled_apps[0];
    
    polled_app_init(app0, 0);
    polled_ctx_init(&app0->ctxs[0], 0, 0);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(app0, 0);

    remove_ctx_from_active(app0, &app0->ctxs[0]);
    test_assert("head is invalid", app0->act_ctx_head == IDXLIST_INVAL);
    test_assert("tail is invalid", app0->act_ctx_tail == IDXLIST_INVAL);
    test_assert("ctx0 is not active", 
            (app0->ctxs[0].flags & FLAG_ACTIVE) == 0);
}

void test_remove_multiple_ctx(void *arg)
{
    struct polled_app *app0, *app1;
    struct dataplane_context *ctx;

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    app0 = &ctx->polled_apps[0];
    app1 = &ctx->polled_apps[1];
    
    polled_app_init(app0, 0);
    polled_ctx_init(&app0->ctxs[0], 0, 0);
    polled_ctx_init(&app0->ctxs[1], 1, 0);
    polled_ctx_init(&app0->ctxs[2], 2, 0);

    polled_app_init(app1, 1);
    polled_ctx_init(&app1->ctxs[0], 0, 1);
    polled_ctx_init(&app1->ctxs[1], 1, 1);
    polled_ctx_init(&app1->ctxs[2], 2, 1);

    ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    ctx->act_head = IDXLIST_INVAL;
    ctx->act_tail = IDXLIST_INVAL;

    enqueue_ctx_to_active(app0, 0);
    enqueue_ctx_to_active(app0, 1);
    enqueue_ctx_to_active(app0, 2);

    enqueue_ctx_to_active(app1, 0);
    enqueue_ctx_to_active(app1, 1);
    enqueue_ctx_to_active(app1, 2);

    /* Remove contexts from app 0 */
    remove_ctx_from_active(app0, &app0->ctxs[1]);
    test_assert("app0: ctx0 is head", app0->act_ctx_head == 0);
    test_assert("app0: ctx2 is tail", app0->act_ctx_tail == 2);
    test_assert("app0: ctx1 is not active", 
            (app0->ctxs[1].flags & FLAG_ACTIVE) == 0);

    remove_ctx_from_active(app0, &app0->ctxs[2]);
    test_assert("app0: ctx0 is head", app0->act_ctx_head == 0);
    test_assert("app0: ctx0 is tail", app0->act_ctx_tail == 0);
    test_assert("app0: ctx2 is not active", 
            (app0->ctxs[2].flags & FLAG_ACTIVE) == 0);

    remove_ctx_from_active(app0, &app0->ctxs[0]);
    test_assert("app0: head is invalid", app0->act_ctx_head == IDXLIST_INVAL);
    test_assert("app0: tail is invalid", app0->act_ctx_tail == IDXLIST_INVAL);
    test_assert("app0: ctx0 is not active", 
            (app0->ctxs[0].flags & FLAG_ACTIVE) == 0);

    /* Remove contexts from app 1 */
    remove_ctx_from_active(app1, &app1->ctxs[1]);
    test_assert("app1: ctx0 is head", app1->act_ctx_head == 0);
    test_assert("app1: ctx2 is tail", app1->act_ctx_tail == 2);
    test_assert("app1: ctx1 is not active", 
            (app1->ctxs[1].flags & FLAG_ACTIVE) == 0);

    remove_ctx_from_active(app1, &app1->ctxs[2]);
    test_assert("app1: ctx0 is head", app1->act_ctx_head == 0);
    test_assert("app1: ctx0 is tail", app1->act_ctx_tail == 0);
    test_assert("app1: ctx2 is not active", 
            (app1->ctxs[2].flags & FLAG_ACTIVE) == 0);

    remove_ctx_from_active(app1, &app1->ctxs[0]);
    test_assert("app1: head is invalid", app1->act_ctx_head == IDXLIST_INVAL);
    test_assert("app1: tail is invalid", app1->act_ctx_tail == IDXLIST_INVAL);
    test_assert("app1: ctx0 is not active", 
            (app1->ctxs[0].flags & FLAG_ACTIVE) == 0);
}

int main(int argc, char *argv[])
{
    int ret = 0;

    if (test_subcase("enqueue app", test_enqueue_app, NULL))
        ret = 1;
    
    if (test_subcase("enqueue ctx", test_enqueue_ctx, NULL))
        ret = 1;

    if (test_subcase("remove app", test_remove_app, NULL))
        ret = 1;

    if (test_subcase("remove app head", test_remove_app_head, NULL))
        ret = 1;

    if (test_subcase("remove app tail", test_remove_app_tail, NULL))
        ret = 1;

    if (test_subcase("remove app one elt", test_remove_app_one_elt, NULL))
        ret = 1;

    if (test_subcase("remove mult apps", test_remove_multiple_apps, NULL))
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