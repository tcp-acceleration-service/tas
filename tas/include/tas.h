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

#ifndef TAS_H_
#define TAS_H_

#include <tas_memif.h>
#include <config.h>
#include <packet_defs.h>

/** @addtogroup tas
 *  @brief TAS.
 */

extern struct configuration config;

extern void *tas_shm;
extern struct flextcp_pl_mem *fp_state;
extern struct flexnic_info *tas_info;
#if RTE_VER_YEAR < 19
  extern struct ether_addr eth_addr;
#else
  extern struct rte_ether_addr eth_addr;
#endif
extern unsigned fp_cores_max;


int slowpath_main(void);

int shm_preinit(void);
int shm_init(unsigned num);
void shm_cleanup(void);
void shm_set_ready(void);

int network_init(unsigned num_threads);
void network_cleanup(void);

/* used by trace and shm */
void *util_create_shmsiszed(const char *name, size_t size, void *addr);

struct notify_blockstate {
  uint64_t last_active_ts;
  int can_block;
  int second_bar;
};

void notify_fastpath_core(unsigned core);
void notify_appctx(struct flextcp_pl_appctx *ctx, uint64_t tsc);
void notify_app_core(int appfd, uint64_t *last_tsc);
void notify_slowpath_core(void);
int notify_canblock(struct notify_blockstate *nbs, int had_data, uint64_t tsc);
void notify_canblock_reset(struct notify_blockstate *nbs);

/* should become config options */
#define FLEXNIC_INTERNAL_MEM_SIZE (1024 * 1024 * 32)
#define FLEXNIC_NUM_QMQUEUES (128 * 1024)

#endif /* ndef TAS_H_ */
