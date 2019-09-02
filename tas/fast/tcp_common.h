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

#ifndef TCP_COMMON_H_
#define TCP_COMMON_H_

#include <tas_memif.h>
#include <utils.h>

#define ALLOW_FUTURE_ACKS 1

/**
 * Check if received packet with sequence number #pkt_seq and #pkt_bytes bytes
 * of payload should be processed or dropped.
 *
 * @param fs        Pointer to flow state.
 * @param pkt_seq   Sequence number of received packet.
 * @param pkt_bytes Received payload length.
 * @param [out] trim_start  If packet is to be processed, indicates how many
 *                          bytes to trim off from beginning of packet.
 * @param [out] trim_end    If packet is to be processed, indicates how many
 *                          bytes to trim off from end of packet.
 *
 * @return 0 if packet should be processed, != 0 to drop.
 */
static inline int tcp_valid_rxseq(struct flextcp_pl_flowst *fs,
    uint32_t pkt_seq, uint16_t pkt_bytes, uint16_t *trim_start,
    uint16_t *trim_end)
{
  uint32_t pseq_a = pkt_seq, pseq_b = pkt_seq + pkt_bytes;
  uint32_t sseq_a = fs->rx_next_seq, sseq_b = fs->rx_next_seq + fs->rx_avail;

  if (pseq_a <= pseq_b && sseq_a <= sseq_b) {
    /* neither packet interval nor receive buffer split */

    /* packet ends before start of receive buffer */
    if (pseq_b < sseq_a)
      return -1;

    /* packet starts after beginning of receive buffer */
    if (pseq_a > sseq_a)
      return -1;

    *trim_start = sseq_a - pseq_a;
    *trim_end = (pseq_b > sseq_b ? pseq_b - sseq_b : 0);
  } else if (pseq_a <= pseq_b && sseq_a > sseq_b) {
    /* packet interval not split, but receive buffer split */

    /* packet ends before start of receive buffer */
    if (pseq_a >= sseq_b && pseq_b < sseq_a)
      return -1;

    /* packet starts after beginning of receive buffer */
    if (pseq_a > sseq_a || pseq_a < sseq_b)
      return -1;

    *trim_start = sseq_a - pseq_a;
    *trim_end = 0;
  } else if (pseq_a > pseq_b && sseq_a <= sseq_b) {
    /* packet interval split, receive buffer not split */

    /* packet ends before start of receive buffer */
    if (pseq_b < sseq_a)
      return -1;

    /* packet starts after beginning of receive buffer */
    if (pseq_a > sseq_a)
      return -1;

    *trim_start = sseq_a - pseq_a;
    *trim_end = (pseq_b > sseq_b ? pseq_b - sseq_b : 0);
  } else {
    /* both intervals split
     * Note this means that there is at least some overlap. */

    /* packet starts after beginning of receive buffer */
    if (pseq_a > sseq_a)
      return -1;

    *trim_start = sseq_a - pseq_a;
    *trim_end = (pseq_b > sseq_b ? pseq_b - sseq_b : 0);
  }

  return 0;
}

/**
 * Check if part of this packet fits into the un-used portion of the receive
 * buffer, even if out of order.
 *
 * @param fs        Pointer to flow state.
 * @param pkt_seq   Sequence number of received packet.
 * @param pkt_bytes Received payload length.
 * @param [out] trim_start  If packet is to be processed, indicates how many
 *                          bytes to trim off from beginning of packet.
 * @param [out] trim_end    If packet is to be processed, indicates how many
 *                          bytes to trim off from end of packet.
 *
 * @return 0 if packet should be processed, != 0 to drop.
 */
static inline int tcp_trim_rxbuf(struct flextcp_pl_flowst *fs,
    uint32_t pkt_seq, uint16_t pkt_bytes, uint16_t *trim_start,
    uint16_t *trim_end)
{
  uint32_t pseq_a = pkt_seq, pseq_b = pkt_seq + pkt_bytes;
  uint32_t sseq_a = fs->rx_next_seq, sseq_b = fs->rx_next_seq + fs->rx_avail;

  if (pseq_a <= pseq_b && sseq_a <= sseq_b) {
    /* neither packet interval nor receive buffer split */

    /* packet ends before start of receive buffer */
    if (pseq_b < sseq_a)
      return -1;

    /* packet starts after end of receive buffer */
    if (pseq_a > sseq_b)
      return -1;

    *trim_start = (pseq_a < sseq_a ? sseq_a - pseq_a : 0);
    *trim_end = (pseq_b > sseq_b ? pseq_b - sseq_b : 0);
  } else if (pseq_a <= pseq_b && sseq_a > sseq_b) {
    /* packet interval not split, but receive buffer split */

    /* packet ends before start of receive buffer */
    if (pseq_a > sseq_b && pseq_b < sseq_a)
      return -1;

    *trim_start = (pseq_a > sseq_b && pseq_a < sseq_a ? sseq_a - pseq_a : 0);
    *trim_end = (pseq_b >= sseq_b && pseq_b < sseq_a ? pseq_b - sseq_b : 0);
  } else if (pseq_a > pseq_b && sseq_a <= sseq_b) {
    /* packet interval split, receive buffer not split */

    /* packet ends before start of receive buffer */
    if (pseq_a > sseq_b && pseq_b < sseq_a)
      return -1;

    *trim_start = (sseq_a <= pseq_b || sseq_a > pseq_a ? sseq_a - pseq_a : 0);
    *trim_end = (pseq_b > sseq_b || sseq_a >= pseq_a ? pseq_b - sseq_b : 0);
  } else {
    /* both intervals split
     * Note this means that there is at least some overlap. */
    *trim_start = (pseq_a < sseq_a ? sseq_a - pseq_a: 0);
    *trim_end = (pseq_b > sseq_b ? pseq_b - sseq_b : 0);
  }

  return 0;
}

/**
 * Check if received ack number is valid, i.e. greater than or equal to the
 * current ack number.
 *
 * @param fs  Pointer to flow state.
 * @param ack Acknowledgement number from package.
 * @param [out] bump num
 *
 * @return 0 if ack is valid, != 0 otherwise
 */
static inline int tcp_valid_rxack(struct flextcp_pl_flowst *fs, uint32_t ack,
    uint32_t *bump)
{
  uint32_t fsack_a = fs->tx_next_seq  - fs->tx_sent, fsack_b = fs->tx_next_seq;

#ifdef ALLOW_FUTURE_ACKS
  /* number of available unsent bytes in send buffer */
  fsack_b += fs->tx_avail;
#endif

  if (fsack_a <= fsack_b) {
    if (ack < fsack_a || ack > fsack_b)
      return -1;

    *bump = ack - fsack_a;
    return 0;
  } else {
    if (fsack_a > ack && ack > fsack_b)
      return -1;

    *bump = ack - fsack_a;
    return 0;
  }
}

/**
 * Calculate how many bytes can be sent based on unsent bytes in send buffer and
 * flow control window
 *
 * @param fs Pointer to flow state.
 * @param [in] phead Pointer to avail, used instead of fs->tx_avail if not NULL.
 *
 * @return Bytes that can be sent.
 */
static inline uint32_t tcp_txavail(const struct flextcp_pl_flowst *fs,
    const uint32_t *pavail)
{
  uint32_t buf_avail, fc_avail;

  buf_avail = (pavail != NULL ? *pavail : fs->tx_avail);

  /* flow control window */
  fc_avail = fs->rx_remote_avail - fs->tx_sent;

  return MIN(buf_avail, fc_avail);
}

/** Pointers to parsed TCP options */
struct tcp_opts {
  /** Timestamp option */
  struct tcp_timestamp_opt *ts;
};

/**
 * Parse TCP option list. For each parsed option the corresponding pointer in
 * opts will be set.
 *
 * @param p Pointer to packet
 * @param len Packet length
 * @param [out] opts Pointers to parsed options
 *
 * @return 0 if parsed successful, -1 otherwise.
 */
static inline int tcp_parse_options(const struct pkt_tcp *p, uint16_t len,
    struct tcp_opts *opts)
{
  uint8_t *opt = (uint8_t *) (p + 1);
  uint16_t opts_len = TCPH_HDRLEN(&p->tcp) * 4 - 20;
  uint16_t off = 0;
  uint8_t opt_kind, opt_len, opt_avail;

  opts->ts = NULL;

  /* whole header not in buf */
  if (TCPH_HDRLEN(&p->tcp) < 5 || opts_len > (len - sizeof(*p))) {
    fprintf(stderr, "hlen=%u opts_len=%u len=%u so=%zu\n", TCPH_HDRLEN(&p->tcp), opts_len, len, sizeof(*p));
    return -1;
  }

  while (off < opts_len) {
    opt_kind = opt[off];
    opt_avail = opts_len - off;
    if (opt_kind == TCP_OPT_END_OF_OPTIONS) {
      /* end of options list option */
      break;
    } else if (opt_kind == TCP_OPT_NO_OP) {
      /* no-op, ignore */
      opt_len = 1;
    } else {
      /* variable length option */
      if (opt_avail < 2) {
        fprintf(stderr, "parse_options: opt_avail=%u kind=%u off=%u\n", opt_avail, opt_kind,  off);
        return -1;
      }

      opt_len = opt[off + 1];
      if (opt_kind == TCP_OPT_TIMESTAMP) {
        if (opt_len != sizeof(struct tcp_timestamp_opt)) {
          fprintf(stderr, "parse_options: opt_len=%u so=%zu\n", opt_len, sizeof(struct tcp_timestamp_opt));
          return -1;
        }

        opts->ts = (struct tcp_timestamp_opt *) (opt + off);
      }
    }
    off += opt_len;
  }

  return 0;
}

#endif /* ndef TCP_COMMON_H_ */
