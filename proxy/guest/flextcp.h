#ifndef V_FLEXTCP_H_
#define V_FLEXTCP_H_

#include "internal.h"

int vflextcp_init(struct guest_proxy *pxy);
int vflextcp_poll(struct guest_proxy *pxy);
int vflextcp_serve_tasinfo(uint8_t *info, ssize_t size);
int vflextcp_write_context_res(struct guest_proxy *pxy,
    int app_idx, uint8_t *resp_buf, size_t resp_sz);
int vflextcp_poke(struct guest_proxy *pxy, int virt_fd);

#endif /* ndef V_FLEXTCP_H */
