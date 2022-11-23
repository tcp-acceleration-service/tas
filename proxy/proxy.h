#ifndef PROXY_H_H
#define PROXY_H_H

#include "../include/kernel_appif.h"

#define EP_LISTEN (NULL)
#define EP_NOTIFY ((void *) (-1))

#define HOST_PEERID 255

struct proxy_context_req {
  int conn_evfd;
  int virt_evfd;
  int app_id;
  struct kernel_uxsock_request req;
};

#endif /* ndef PROXY_H_H */