/*
 * Copyright 2020 University of Washington, Max Planck Institute for
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

#include <assert.h>
#include <unistd.h>

#include <tas.h>

static void util_flexnic_kick(struct flextcp_pl_appctx *ctx, uint32_t ts_us)
{
  if(ts_us - ctx->last_ts > POLL_CYCLE) {
    // Kick kernel
    //fprintf(stderr, "kicking app/flexnic on %d in %p, &evfd: %p\n", ctx->evfd, ctx, &(ctx->evfd));
    uint64_t val = 1;
    int r = write(ctx->evfd, &val, sizeof(uint64_t));
    assert(r == sizeof(uint64_t));
  }

  ctx->last_ts = ts_us;
}

void notify_fastpath_core(unsigned core, uint32_t ts)
{
  util_flexnic_kick(&fp_state->kctx[core], ts);
}

void notify_appctx(struct flextcp_pl_appctx *ctx, uint32_t ts_us)
{
  util_flexnic_kick(ctx, ts_us);
}
