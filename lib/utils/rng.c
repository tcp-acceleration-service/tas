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

#include <utils_rng.h>

static const uint64_t a = 0x5deece66dULL;
static const uint64_t c = 0xb;
static const uint64_t m = 1ULL << 48;

void utils_rng_init(struct utils_rng *rng, uint64_t seed)
{
    rng->seed = (seed ^ a) % m;
}

uint32_t utils_rng_gen32(struct utils_rng *rng)
{
    uint64_t next;
    next = (a * rng->seed + c) % m;
    rng->seed = next;
    return next >> 16;
}

double utils_rng_gend(struct utils_rng *rng)
{
    // This is what Java seems to do
    uint64_t x =
            (((uint64_t) utils_rng_gen32(rng) >> 6) << 27) +
            (utils_rng_gen32(rng) >> 5);
    return x / ((double) (1ULL << 53));
}

void utils_rng_gen(struct utils_rng *rng, void *buf, size_t size)
{
    uint32_t x;
    while (size >= 4) {
        * ((uint32_t *) buf) = utils_rng_gen32(rng);
        buf = (void*) ((uintptr_t) buf + 4);
        size -= 4;
    }

    x = utils_rng_gen32(rng);
    while (size > 0) {
        * ((uint8_t *) buf) = x >> 24;
        x <<= 8;
        buf = (void*) ((uintptr_t) buf + 1);
        size--;
    }

}

