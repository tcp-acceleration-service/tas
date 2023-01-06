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

#ifndef FLEXNIC_PXY_H_
#define FLEXNIC_PXY_H_

#include <tas_ll.h>
#include <tas_memif.h>

#include "../internal.h"

extern void *flexnic_mem_pxy[FLEXNIC_PL_VMST_NUM];
extern int flexnic_shmfds_pxy[FLEXNIC_PL_VMST_NUM];
extern struct flexnic_info *flexnic_info_pxy;

int flextcp_proxy_init();
int flextcp_proxy_context_create(struct flextcp_context *ctx,
    uint8_t *presp, ssize_t *presp_sz, int vmid, int appid);
int flextcp_proxy_newapp(int vmid, int appid);

#endif /* ndef FLEXNIC_PXY_H_ */
