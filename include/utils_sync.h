/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* spinlock implementation from DPDK (copied to remove dependencies on dpdk
 * includes everywhere) */

#ifndef UTILS_SYNC_H_
#define UTILS_SYNC_H_

#include <stdint.h>

static inline void util_spin_lock(volatile uint32_t *sl)
{
  uint32_t lock_val = 1;

  asm volatile (
      "1:\n"
      "xchg %[locked], %[lv]\n"
      "test %[lv], %[lv]\n"
      "jz 3f\n"
      "2:\n"
      "pause\n"
      "cmpl $0, %[locked]\n"
      "jnz 2b\n"
      "jmp 1b\n"
      "3:\n"
      : [locked] "=m" (*sl), [lv] "=q" (lock_val)
      : "[lv]" (lock_val)
      : "memory");
}

static inline void util_spin_unlock(volatile uint32_t *sl)
{
  uint32_t unlock_val = 0;

  asm volatile (
      "xchg %[locked], %[ulv]\n"
      : [locked] "=m" (*sl), [ulv] "=q" (unlock_val)
      : "[ulv]" (unlock_val)
      : "memory");
}

static inline int util_spin_trylock(volatile uint32_t *sl)
{
  uint32_t lockval = 1;

  asm volatile (
      "xchg %[locked], %[lockval]"
      : [locked] "=m" (*sl), [lockval] "=q" (lockval)
      : "[lockval]" (lockval)
      : "memory");

  return lockval == 0;
}
#endif /* ndef UTILS_SYNC_H_ */
