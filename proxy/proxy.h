#ifndef PROXY_H_H
#define PROXY_H_H

#include "../include/kernel_appif.h"

#define EP_LISTEN (NULL)
#define EP_NOTIFY ((void *) (-1))

#define HOST_PEERID 255

#define CHANNEL_POLL_ROUNDS 10

struct proxy_context_req {
  int actx_evfd;
  int ctxreq_id;
  int app_id;
  struct kernel_uxsock_request req;
} __attribute((packed))__;

#endif /* ndef PROXY_H_H */