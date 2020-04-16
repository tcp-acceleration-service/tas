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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <utils.h>
#include <assert.h>
#include <unistd.h>

int util_parse_ipv4(const char *s, uint32_t *ip)
{
  if (inet_pton(AF_INET, s, ip) != 1) {
    return -1;
  }
  *ip = htonl(*ip);
  return 0;
}

int util_parse_mac(const char *s, uint64_t *mac)
{
  char buf[18];
  int i;
  uint64_t x, y;

  /* mac address has length 17 = 2x6 digits + 5 colons */
  if (strlen(s) != 17 || s[2] != ':' || s[5] != ':' ||
      s[8] != ':' || s[11] != ':' || s[14] != ':')
  {
    return -1;
  }
  memcpy(buf, s, sizeof(buf));

  /* replace colons by NUL bytes to separate strings */
  buf[2] = buf[5] = buf[8] = buf[11] = buf[14] = 0;

  y = 0;
  for (i = 5; i >= 0; i--) {
      if (!isxdigit(buf[3 * i]) || !isxdigit(buf[3 * i + 1])) {
          return -1;
      }
      x = strtoul(&buf[3 * i], NULL, 16);
      y = (y << 8) | x;
  }
  *mac = y;

  return 0;
}

void util_dump_mem(const void *mem, size_t len)
{
  const uint8_t *b = mem;
  size_t i;
  for (i = 0; i < len; i++) {
    fprintf(stderr, "%02x ", b[i]);
  }
  fprintf(stderr, "\n");
}
