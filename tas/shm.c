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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>

#include <utils.h>
#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include <tas.h>
#include <utils_shm.h>
#include <tas_memif.h>

void *tas_shm = NULL;
int tas_shm_fd = -1;
struct flextcp_pl_mem *fp_state = NULL;
struct flexnic_info *tas_info = NULL;

/* convert microseconds to cycles */
static uint64_t us_to_cycles(uint32_t us);

/* Allocate DMA memory before DPDK grabs all huge pages */
int shm_preinit(void)
{
  /* create shm for dma memory */
  if (config.fp_hugepages) {
    tas_shm = util_create_shmsiszed_huge(FLEXNIC_NAME_DMA_MEM,
        config.shm_len, NULL, &tas_shm_fd, FLEXNIC_HUGE_PREFIX);
  } else {
    tas_shm = util_create_shmsiszed(FLEXNIC_NAME_DMA_MEM, config.shm_len,
        NULL, &tas_shm_fd);
  }
  if (tas_shm == NULL) {
    fprintf(stderr, "mapping flexnic dma memory failed\n");
    return -1;
  }

  /* create shm for internal memory */
  if (config.fp_hugepages) {
    fp_state = util_create_shmsiszed_huge(FLEXNIC_NAME_INTERNAL_MEM,
        FLEXNIC_INTERNAL_MEM_SIZE, NULL, NULL, FLEXNIC_HUGE_PREFIX);
  } else {
    fp_state = util_create_shmsiszed(FLEXNIC_NAME_INTERNAL_MEM,
        FLEXNIC_INTERNAL_MEM_SIZE, NULL, NULL);
  }
  if (fp_state == NULL) {
    fprintf(stderr, "mapping flexnic internal memory failed\n");
    shm_cleanup();
    return -1;
  }

  return 0;
}

int shm_init(unsigned num)
{
  umask(0);

  /* create shm for tas_info */
  tas_info = util_create_shmsiszed(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES,
      NULL, NULL);
  if (tas_info == NULL) {
    fprintf(stderr, "mapping flexnic tas_info failed\n");
    shm_cleanup();
    return -1;
  }

  tas_info->dma_mem_size = config.shm_len;
  tas_info->dma_mem_off = 0;
  tas_info->internal_mem_size = FLEXNIC_INTERNAL_MEM_SIZE;
  tas_info->qmq_num = FLEXNIC_NUM_QMQUEUES;
  tas_info->cores_num = num;
  tas_info->mac_address = 0;
  tas_info->poll_cycle_app = us_to_cycles(config.fp_poll_interval_app);
  tas_info->poll_cycle_tas = us_to_cycles(config.fp_poll_interval_tas);

  if (config.fp_hugepages)
    tas_info->flags |= FLEXNIC_FLAG_HUGEPAGES;

  return 0;
}

void shm_cleanup(void)
{
  /* cleanup internal memory region */
  if (fp_state != NULL) {
    if (config.fp_hugepages) {
      util_destroy_shm_huge(FLEXNIC_NAME_INTERNAL_MEM, FLEXNIC_INTERNAL_MEM_SIZE,
          fp_state, FLEXNIC_HUGE_PREFIX);
    } else {
      util_destroy_shm(FLEXNIC_NAME_INTERNAL_MEM, FLEXNIC_INTERNAL_MEM_SIZE,
          fp_state);
    }
  }

  /* cleanup dma memory region */
  if (tas_shm != NULL) {
    if (config.fp_hugepages) {
      util_destroy_shm_huge(FLEXNIC_NAME_DMA_MEM, config.shm_len, tas_shm,
          FLEXNIC_HUGE_PREFIX);
    } else {
      util_destroy_shm(FLEXNIC_NAME_DMA_MEM, config.shm_len, tas_shm);
    }
  }

  /* cleanup tas_info memory region */
  if (tas_info != NULL) {
    util_destroy_shm(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES, tas_info);
  }
}

void shm_set_ready(void)
{
  tas_info->flags |= FLEXNIC_FLAG_READY;
}

static uint64_t us_to_cycles(uint32_t us)
{
  if (us == UINT32_MAX) {
    return UINT64_MAX;
  }

  return (rte_get_tsc_hz() * us) / 1000000;
}

