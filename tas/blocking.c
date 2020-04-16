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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include <tas.h>
#include <utils_timeout.h>

extern int kernel_notifyfd;

static void notify_core(int cfd, uint32_t *last_ts, uint32_t ts_us,
    uint32_t delta)
{
  uint64_t val;

  if(ts_us - *last_ts > delta) {
    val = 1;
    if (write(cfd, &val, sizeof(uint64_t)) != sizeof(uint64_t)) {
      perror("notify_core: write failed");
      abort();
    }
  }

  *last_ts = ts_us;
}

void notify_fastpath_core(unsigned core)
{
  notify_core(fp_state->kctx[core].evfd, &fp_state->kctx[core].last_ts,
      util_timeout_time_us(), tas_info->poll_cycle_tas);
}

void notify_app_core(int appfd, uint32_t *last_ts)
{
  notify_core(appfd, last_ts, util_timeout_time_us(), tas_info->poll_cycle_app);
}

void notify_appctx(struct flextcp_pl_appctx *ctx, uint32_t ts_us)
{
  notify_core(ctx->evfd, &ctx->last_ts, ts_us, tas_info->poll_cycle_app);
}

void notify_slowpath_core(void)
{
  static uint32_t __thread last_ts = 0;
  notify_core(kernel_notifyfd, &last_ts, util_timeout_time_us(),
      tas_info->poll_cycle_tas);
}

int notify_canblock(struct notify_blockstate *nbs, int had_data, uint32_t ts)
{
  if (had_data) {
    /* not idle this round, reset everything */
    nbs->can_block = nbs->second_bar = 0;
    nbs->last_active_ts = ts;
  } else if (nbs->second_bar) {
    /* we can block now, reset afterwards */
    nbs->can_block = nbs->second_bar = 0;
    nbs->last_active_ts = ts;
    return 1;
  } else if (nbs->can_block &&
      ts - nbs->last_active_ts > tas_info->poll_cycle_tas)
  {
    /* we've reached the poll cycle interval, so just poll once more */
    nbs->second_bar = 1;
  } else {
    /* waiting for poll cycle interval */
    nbs->can_block = 1;
  }

  return 0;
}

void notify_canblock_reset(struct notify_blockstate *nbs)
{
  nbs->can_block = nbs->second_bar = 0;
}
