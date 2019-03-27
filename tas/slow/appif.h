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

#ifndef APPIF_H_
#define APPIF_H_

#include <stdbool.h>
#include <stdint.h>

#include "internal.h"
#include <kernel_appif.h>

struct app_doorbell {
  uint32_t id;
  /* only for freelist */
  struct app_doorbell *next;
};

struct app_context {
  struct application *app;
  struct packetmem_handle *kin_handle;
  void *kin_base;
  uint32_t kin_len;
  uint32_t kin_pos;

  struct packetmem_handle *kout_handle;
  void *kout_base;
  uint32_t kout_len;
  uint32_t kout_pos;

  struct app_doorbell *doorbell;

  int ready, evfd;
  uint32_t last_ts;
  struct app_context *next;

  struct {
    struct packetmem_handle *rxq;
    struct packetmem_handle *txq;
  } handles[];
};

struct application {
  int fd;
  struct nbqueue_el nqe;
  size_t req_rx;
  struct kernel_uxsock_request req;
  size_t resp_sz;
  struct kernel_uxsock_response *resp;

  struct app_context *contexts;
  struct application *next;
  struct app_context *need_reg_ctx;
  struct app_context *need_reg_ctx_done;

  struct connection *conns;
  struct listener   *listeners;

  struct nicif_completion comp;

  uint16_t id;
  volatile bool closed;
};

/**
 * Poll kernel->app context queue.
 *
 * @param app Application to poll
 * @param ctx Context to poll
 */
unsigned appif_ctx_poll(struct application *app, struct app_context *ctx);

#endif /* ndef APPIF_H_ */
