#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>


#include <tas_sockets.h>

#include "../testutils.h"
#include "harness.h"

#define TEST_IP   0x0a010203
#define TEST_PORT 12345

#define TEST_LIP   0x0a010201
#define TEST_LPORT 23456

static void test_connect_success(void *p)
{
  int fd, flag, ret;
  struct sockaddr_in addr;
  uint64_t opaque;
  void *rxbuf, *txbuf;
  socklen_t slen;
  int status;

  fd = tas_socket(AF_INET, SOCK_STREAM, 0);
  test_assert("socket connect", fd > 0);

  flag = tas_fcntl(fd, F_GETFL, 0);
  test_assert("fcntl getfl success", flag >= 0);

  flag |= O_NONBLOCK;
  ret = tas_fcntl(fd, F_SETFL, flag);
  test_assert("fcntl setfl success", ret >= 0);

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(TEST_IP);
  addr.sin_port = htons(TEST_PORT);
  ret = tas_connect(fd, (struct sockaddr *) &addr, sizeof(addr));
  test_assert("tas_connect success", ret < 0 && errno == EINPROGRESS);

  /* check aout entry for new connection request */
  ret = harness_aout_pull_connopen_op(0, &opaque, TEST_IP, TEST_PORT, 0);
  test_assert("pulling conn open request off aout", ret == 0);

  /* push ain entry for new connection response */
  rxbuf = test_zalloc(1024);
  txbuf = test_zalloc(1024);
  ret = harness_ain_push_connopened(0, opaque, 1024, rxbuf, 1024,
      txbuf, 1, TEST_LIP, TEST_LPORT, 0);
  test_assert("harness_ain_push_connopened success", ret == 0);

  slen = sizeof(status);
  ret = tas_getsockopt(fd, SOL_SOCKET, SO_ERROR, &status, &slen);
  test_assert("tas_getsockopt getstatus success", ret == 0);
  test_assert("tas_getsockopt status done", status == 0);
}

static void test_connect_fail(void *p)
{
  int fd, flag, ret;
  struct sockaddr_in addr;
  uint64_t opaque;
  socklen_t slen;
  int status;

  fd = tas_socket(AF_INET, SOCK_STREAM, 0);
  test_assert("socket connect", fd > 0);

  flag = tas_fcntl(fd, F_GETFL, 0);
  test_assert("fcntl getfl success", flag >= 0);

  flag |= O_NONBLOCK;
  ret = tas_fcntl(fd, F_SETFL, flag);
  test_assert("fcntl setfl success", ret >= 0);

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(TEST_IP);
  addr.sin_port = htons(TEST_PORT);
  ret = tas_connect(fd, (struct sockaddr *) &addr, sizeof(addr));
  test_assert("tas_connect success", ret < 0 && errno == EINPROGRESS);

  /* check aout entry for new connection request */
  ret = harness_aout_pull_connopen_op(0, &opaque, TEST_IP, TEST_PORT, 0);
  test_assert("pulling conn open request off aout", ret == 0);

  /* push ain entry for new connection response */
  ret = harness_ain_push_connopen_failed(0, opaque, 1);
  test_assert("harness_ain_push_connopen_failed success", ret == 0);

  slen = sizeof(status);
  ret = tas_getsockopt(fd, SOL_SOCKET, SO_ERROR, &status, &slen);
  test_assert("tas_getsockopt getstatus success", ret == 0);
  test_assert("tas_getsockopt status done", status == ECONNREFUSED);
}


int main(int argc, char *argv[])
{
  int ret = 0;

  struct harness_params params;
  params.num_ctxs = 1;
  params.fp_cores = 2;
  params.arx_len = 1024;
  params.atx_len = 1024;
  params.ain_len = 1024;
  params.aout_len = 1024;

  harness_prepare(&params);

  if (test_subcase("connect success", test_connect_success, NULL))
    ret = 1;

  if (test_subcase("connect fail", test_connect_fail, NULL))
    ret = 1;

  return ret;
}
