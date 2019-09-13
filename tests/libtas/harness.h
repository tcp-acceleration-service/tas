#ifndef HARNESS_H_
#define HARNESS_H_

#include <stddef.h>
#include <stdint.h>

#include <kernel_appif.h>
#include <tas_memif.h>

struct harness_params {
  size_t num_ctxs;
  size_t fp_cores;
  size_t arx_len;
  size_t atx_len;
  size_t ain_len;
  size_t aout_len;
};

void harness_prepare(struct harness_params *hp);

int harness_aout_peek(struct kernel_appout **ao, size_t ctxid);
int harness_aout_pop(size_t ctxid);
int harness_aout_pull_connopen(size_t ctxid, uint64_t opaque, uint32_t remote_ip,
    uint16_t remote_port, uint8_t flags);
int harness_aout_pull_connopen_op(size_t ctxid, uint64_t *opaque,
    uint32_t remote_ip, uint16_t remote_port, uint8_t flags);

int harness_ain_push(size_t ctxid, struct kernel_appin *ai);
int harness_ain_push_connopened(size_t ctxid, uint64_t opaque, uint32_t rx_len,
    void *rx_buf, uint32_t tx_len, void *tx_buf, uint32_t flow_id,
    uint32_t local_ip, uint16_t local_port, uint32_t core);
int harness_ain_push_connopen_failed(size_t ctxid, uint64_t opaque,
    int32_t status);

int harness_atx_pull(size_t ctxid, size_t qid, uint32_t rx_bump,
    uint32_t tx_bump, uint32_t flow_id, uint16_t bump_seq, uint8_t flags);
int harness_arx_push(size_t ctxid, size_t qid, uint64_t opaque,
    uint32_t rx_bump, uint32_t rx_pos, uint32_t tx_bump, uint8_t flags);

#endif // ndef HARNESS_H_
