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

#include <tas.h>
#include <tas_memif.h>

void *tas_shm = NULL;
struct flextcp_pl_mem *fp_state = NULL;
struct flexnic_info *tas_info = NULL;

/* destroy shared memory region */
static void destroy_shm(const char *name, size_t size, void *addr);
/* create shared memory region using huge pages */
static void *util_create_shmsiszed_huge(const char *name, size_t size,
    void *addr) __attribute__((used));
/* destroy shared huge page memory region */
static void destroy_shm_huge(const char *name, size_t size, void *addr)
    __attribute__((used));

/* Allocate DMA memory before DPDK grabs all huge pages */
int shm_preinit(void)
{
  /* create shm for dma memory */
  if (config.fp_hugepages) {
    tas_shm = util_create_shmsiszed_huge(FLEXNIC_NAME_DMA_MEM,
        FLEXNIC_DMA_MEM_SIZE, NULL);
  } else {
    tas_shm = util_create_shmsiszed(FLEXNIC_NAME_DMA_MEM, FLEXNIC_DMA_MEM_SIZE,
        NULL);
  }
  if (tas_shm == NULL) {
    fprintf(stderr, "mapping flexnic dma memory failed\n");
    return -1;
  }

  /* create shm for internal memory */
  if (config.fp_hugepages) {
    fp_state = util_create_shmsiszed_huge(FLEXNIC_NAME_INTERNAL_MEM,
        FLEXNIC_INTERNAL_MEM_SIZE, NULL);
  } else {
    fp_state = util_create_shmsiszed(FLEXNIC_NAME_INTERNAL_MEM,
        FLEXNIC_INTERNAL_MEM_SIZE, NULL);
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
  tas_info = util_create_shmsiszed(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES, NULL);
  if (tas_info == NULL) {
    fprintf(stderr, "mapping flexnic tas_info failed\n");
    shm_cleanup();
    return -1;
  }

  tas_info->dma_mem_size = FLEXNIC_DMA_MEM_SIZE;
  tas_info->internal_mem_size = FLEXNIC_INTERNAL_MEM_SIZE;
  tas_info->qmq_num = FLEXNIC_NUM_QMQUEUES;
  tas_info->cores_num = num;
  tas_info->mac_address = 0;

  if (config.fp_hugepages)
    tas_info->flags |= FLEXNIC_FLAG_HUGEPAGES;

  return 0;
}

void shm_cleanup(void)
{
  /* cleanup internal memory region */
  if (fp_state != NULL) {
    if (config.fp_hugepages) {
      destroy_shm_huge(FLEXNIC_NAME_INTERNAL_MEM, FLEXNIC_INTERNAL_MEM_SIZE,
          fp_state);
    } else {
      destroy_shm(FLEXNIC_NAME_INTERNAL_MEM, FLEXNIC_INTERNAL_MEM_SIZE,
          fp_state);
    }
  }

  /* cleanup dma memory region */
  if (tas_shm != NULL) {
    if (config.fp_hugepages) {
      destroy_shm_huge(FLEXNIC_NAME_DMA_MEM, FLEXNIC_DMA_MEM_SIZE, tas_shm);
    } else {
      destroy_shm(FLEXNIC_NAME_DMA_MEM, FLEXNIC_DMA_MEM_SIZE, tas_shm);
    }
  }

  /* cleanup tas_info memory region */
  if (tas_info != NULL) {
    destroy_shm(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES, tas_info);
  }
}

void shm_set_ready(void)
{
  tas_info->flags |= FLEXNIC_FLAG_READY;
}

void *util_create_shmsiszed(const char *name, size_t size, void *addr)
{
  int fd;
  void *p;

  if ((fd = shm_open(name, O_CREAT | O_RDWR, 0666)) == -1) {
    perror("shm_open failed");
    goto error_out;
  }
  if (ftruncate(fd, size) != 0) {
    perror("ftruncate failed");
    goto error_remove;
  }

  if ((p = mmap(addr, size, PROT_READ | PROT_WRITE,
      MAP_SHARED | (addr == NULL ? 0 : MAP_FIXED) | MAP_POPULATE, fd, 0)) ==
      (void *) -1)
  {
    perror("mmap failed");
    goto error_remove;
  }

  memset(p, 0, size);

  close(fd);
  return p;

error_remove:
  close(fd);
  shm_unlink(name);
error_out:
  return NULL;
}

static void destroy_shm(const char *name, size_t size, void *addr)
{
  if (munmap(addr, size) != 0) {
    fprintf(stderr, "Warning: munmap failed (%s)\n", strerror(errno));
  }
  shm_unlink(name);
}

static void *util_create_shmsiszed_huge(const char *name, size_t size,
    void *addr)
{
  int fd;
  void *p;
  char path[128];

  snprintf(path, sizeof(path), "%s/%s", FLEXNIC_HUGE_PREFIX, name);

  if ((fd = open(path, O_CREAT | O_RDWR, 0666)) == -1) {
    perror("util_create_shmsiszed: open failed");
    goto error_out;
  }
  if (ftruncate(fd, size) != 0) {
    perror("util_create_shmsiszed: ftruncate failed");
    goto error_remove;
  }

  if ((p = mmap(addr, size, PROT_READ | PROT_WRITE,
      MAP_SHARED | (addr == NULL ? 0 : MAP_FIXED) | MAP_POPULATE, fd, 0)) ==
      (void *) -1)
  {
    perror("util_create_shmsiszed: mmap failed");
    goto error_remove;
  }

  memset(p, 0, size);

  close(fd);
  return p;

error_remove:
  close(fd);
  shm_unlink(name);
error_out:
  return NULL;
}

static void destroy_shm_huge(const char *name, size_t size, void *addr)
{
  char path[128];

  snprintf(path, sizeof(path), "%s/%s", FLEXNIC_HUGE_PREFIX, name);

  if (munmap(addr, size) != 0) {
    fprintf(stderr, "Warning: munmap failed (%s)\n", strerror(errno));
  }
  unlink(path);
}
