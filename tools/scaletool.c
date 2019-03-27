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

#define _GNU_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <tas_ll.h>
#include <utils.h>

int flextcp_kernel_reqscale(struct flextcp_context *ctx, uint32_t cores);

int main(int argc, char *argv[])
{
    unsigned cores;
    struct flextcp_context ctx;

    if (argc != 2) {
        fprintf(stderr, "Usage: ./scaletool CORES\n");
        return EXIT_FAILURE;
    }

    cores = atoi(argv[1]);

    if (flextcp_init() != 0) {
        fprintf(stderr, "flextcp_init failed\n");
        return EXIT_FAILURE;
    }

    if (flextcp_context_create(&ctx) != 0) {
        fprintf(stderr, "flextcp_context_create failed\n");
        return EXIT_FAILURE;
    }

    if (flextcp_kernel_reqscale(&ctx, cores) != 0) {
        fprintf(stderr, "flextcp_kernel_reqscale failed\n");
        return EXIT_FAILURE;
    }

    sleep(1);

    return EXIT_SUCCESS;
}
