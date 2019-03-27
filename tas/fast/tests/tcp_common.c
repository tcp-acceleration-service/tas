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

#include <stdio.h>

#include <flextcp_plif.h>
#include "../tcp_common.h"

static int test_tcp_valid_rxseq_single(uint32_t fs_seq, uint32_t fs_avail,
    uint32_t pkt_seq, uint32_t pkt_bytes, int process, uint16_t trim_start,
    uint16_t trim_end)
{
  struct flextcp_pl_flowst fs;
  uint16_t ts, te;
  int ret;

  printf("  fs_seq=%u fs_avail=%u pkt_seq=%u pkt_bytes=%u\n", fs_seq, fs_avail,
      pkt_seq, pkt_bytes);

  fs.rx_next_seq = fs_seq;
  fs.rx_avail = fs_avail;

  ret = tcp_valid_rxseq(&fs, pkt_seq, pkt_bytes, &ts, &te);
  if (ret != 0) {
    if (process) {
      fprintf(stderr, "    pkt unexpectedly rejected\n");
      return -1;
    }
    return 0;
  } else if (!process) {
    fprintf(stderr, "    pkt unexpectedly accepted\n");
    return -1;
  }

  if (trim_start != ts || trim_end != te) {
    fprintf(stderr, "    trims don't match: got (ts=%u te=%u) expect "
        "(ts=%u te=%u)\n", ts, te, trim_start, trim_end);
    return -1;
  }

  return 0;
}

static int test_tcp_valid_rxseq(void)
{
  int ret = 0;

  printf("testing tcp_valid_rxseq\n");

  /* fits fully, no wrap-arounds */
  if (test_tcp_valid_rxseq_single(2048, 8192, 2048, 1400, 1, 0, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, no wrap-arounds */
  if (test_tcp_valid_rxseq_single(2048, 8192, 2000, 1400, 1, 48, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, and does not fit fully, no wrap-arounds */
  if (test_tcp_valid_rxseq_single(2048, 1000, 2000, 1400, 1, 48, 352) != 0)
    ret = -1;
  /* starts after beginning, no wrap-arounds */
  if (test_tcp_valid_rxseq_single(2048, 8192, 2049, 1400, 0, 0, 0) != 0)
    ret = -1;
  /* starts after buffer, no wrap-arounds */
  if (test_tcp_valid_rxseq_single(2048, 8192, 16384, 1400, 0, 0, 0) != 0)
    ret = -1;

  /* fits fully, wrap in buffer */
  if (test_tcp_valid_rxseq_single(4294965248, 8192, 4294965248, 1400, 1, 0, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, wrap in buffer */
  if (test_tcp_valid_rxseq_single(4294965248, 8192, 4294965200, 1400, 1, 48, 0) != 0)
    ret = -1;
  /* starts after beginning in upper half, wrap in buffer */
  if (test_tcp_valid_rxseq_single(4294965248, 8192, 4294965249, 1400, 0, 0, 0) != 0)
    ret = -1;
  /* starts after beginning in lower half, wrap in buffer */
  if (test_tcp_valid_rxseq_single(4294965248, 8192, 0, 1400, 0, 0, 0) != 0)
    ret = -1;
  /* starts after buffer, wrap in buffer */
  if (test_tcp_valid_rxseq_single(4294965248, 8192, 6144, 1400, 0, 0, 0) != 0)
    ret = -1;

  /* fits fully, wrap in buffer and packet */
  if (test_tcp_valid_rxseq_single(4294966784, 8192, 4294966784, 1400, 1, 0, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, wrap in buffer and packet */
  if (test_tcp_valid_rxseq_single(4294966784, 8192, 4294966700, 1400, 1, 84, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, and does not fit fully, wrap in buffer and packet */
  if (test_tcp_valid_rxseq_single(4294966784, 1000, 4294966700, 1400, 1, 84, 316) != 0)
    ret = -1;
  /* starts after beginning , wrap in buffer and packet*/
  if (test_tcp_valid_rxseq_single(4294966784, 8192, 4294966785, 1400, 0, 0, 0) != 0)
    ret = -1;

  return ret;
}

static int test_tcp_trim_rxbuf_single(uint32_t fs_seq, uint32_t fs_avail,
    uint32_t pkt_seq, uint32_t pkt_bytes, int process, uint16_t trim_start,
    uint16_t trim_end)
{
  struct flextcp_pl_flowst fs;
  uint16_t ts, te;
  int ret;

  printf("  fs_seq=%u fs_avail=%u pkt_seq=%u pkt_bytes=%u\n", fs_seq, fs_avail,
      pkt_seq, pkt_bytes);

  fs.rx_next_seq = fs_seq;
  fs.rx_avail = fs_avail;

  ret = tcp_trim_rxbuf(&fs, pkt_seq, pkt_bytes, &ts, &te);
  if (ret != 0) {
    if (process) {
      fprintf(stderr, "    pkt unexpectedly rejected\n");
      return -1;
    }
    return 0;
  } else if (!process) {
    fprintf(stderr, "    pkt unexpectedly accepted\n");
    return -1;
  }

  if (trim_start != ts || trim_end != te) {
    fprintf(stderr, "    trims don't match: got (ts=%u te=%u) expect "
        "(ts=%u te=%u)\n", ts, te, trim_start, trim_end);
    return -1;
  }

  return 0;
}

static int test_tcp_trim_rxbuf(void)
{
  int ret = 0;

  printf("testing tcp_trim_rxbuf\n");

  /* fits fully, no wrap-arounds */
  if (test_tcp_trim_rxbuf_single(2048, 8192, 2048, 1400, 1, 0, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, no wrap-arounds */
  if (test_tcp_trim_rxbuf_single(2048, 8192, 2000, 1400, 1, 48, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, and does not fit fully, no wrap-arounds */
  if (test_tcp_trim_rxbuf_single(2048, 1000, 2000, 1400, 1, 48, 352) != 0)
    ret = -1;
  /* starts after beginning, no wrap-arounds */
  if (test_tcp_trim_rxbuf_single(2048, 8192, 2049, 1400, 1, 0, 0) != 0)
    ret = -1;
  /* starts after buffer, no wrap-arounds */
  if (test_tcp_trim_rxbuf_single(2048, 8192, 16384, 1400, 0, 0, 0) != 0)
    ret = -1;

  /* fits fully, wrap in buffer */
  if (test_tcp_trim_rxbuf_single(4294965248, 8192, 4294965248, 1400, 1, 0, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, wrap in buffer */
  if (test_tcp_trim_rxbuf_single(4294965248, 8192, 4294965200, 1400, 1, 48, 0) != 0)
    ret = -1;
  /* starts after beginning in upper half, wrap in buffer */
  if (test_tcp_trim_rxbuf_single(4294965248, 8192, 4294965249, 1400, 1, 0, 0) != 0)
    ret = -1;
  /* starts after beginning in lower half, wrap in buffer */
  if (test_tcp_trim_rxbuf_single(4294965248, 8192, 0, 1400, 1, 0, 0) != 0)
    ret = -1;
  /* starts right after buffer, wrap in buffer */
  if (test_tcp_trim_rxbuf_single(4294965248, 8192, 6144, 1400, 1, 0, 1400) != 0)
    ret = -1;
  /* starts further after buffer, wrap in buffer */
  if (test_tcp_trim_rxbuf_single(4294965248, 8192, 6145, 1400, 0, 0, 0) != 0)
    ret = -1;

  /* fits fully, wrap in buffer and packet */
  if (test_tcp_trim_rxbuf_single(4294966784, 8192, 4294966784, 1400, 1, 0, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, wrap in buffer and packet */
  if (test_tcp_trim_rxbuf_single(4294966784, 8192, 4294966700, 1400, 1, 84, 0) != 0)
    ret = -1;
  /* overlaps already received bytes, and does not fit fully, wrap in buffer and packet */
  if (test_tcp_trim_rxbuf_single(4294966784, 1000, 4294966700, 1400, 1, 84, 316) != 0)
    ret = -1;
  /* starts after beginning , wrap in buffer and packet*/
  if (test_tcp_trim_rxbuf_single(4294966784, 8192, 4294966785, 1400, 1, 0, 0) != 0)
    ret = -1;

  return ret;
}


int main(int argc, char *argv[])
{
  int ret = 0;

  if (test_tcp_valid_rxseq() != 0)
    ret = -1;

  if (test_tcp_trim_rxbuf() != 0)
    ret = -1;

  return ret;
}
