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

#ifndef DMA_H_
#define DMA_H_

#include <stddef.h>
#include <stdint.h>

#include <rte_config.h>
#include <rte_memcpy.h>
#include <tas.h>

#ifdef DATAPLANE_STATS
void dma_dump_stats(void);
#endif

static inline void dma_read(uintptr_t addr, size_t len, void *buf)
{
  assert(addr + len >= addr && addr + len <= config.shm_len);

  rte_memcpy(buf, (uint8_t *) tas_shm + addr, len);

#ifdef FLEXNIC_TRACE_DMA
  struct flexnic_trace_entry_dma evt = {
      .addr = addr,
      .len = len,
    };
  trace_event2(FLEXNIC_TRACE_EV_DMARD, sizeof(evt), &evt,
      MIN(len, UINT16_MAX - sizeof(evt)), buf);
#endif
}

static inline void dma_write(uintptr_t addr, size_t len, const void *buf)
{
  assert(addr + len >= addr && addr + len <= config.shm_len);

  rte_memcpy((uint8_t *) tas_shm + addr, buf, len);

#ifdef FLEXNIC_TRACE_DMA
  struct flexnic_trace_entry_dma evt = {
      .addr = addr,
      .len = len,
    };
  trace_event2(FLEXNIC_TRACE_EV_DMAWR, sizeof(evt), &evt,
      MIN(len, UINT16_MAX - sizeof(evt)), buf);
#endif
}

static inline void *dma_pointer(uintptr_t addr, size_t len)
{
  /* validate address */
  assert(addr + len >= addr && addr + len <= config.shm_len);

  return (uint8_t *) tas_shm + addr;
}

#endif /* ndef DMA_H_ */
