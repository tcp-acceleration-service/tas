#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>

#include "testutils.h"

void *test_zalloc(size_t len)
{
  void *ptr;

  if ((ptr = calloc(1, len)) == NULL) {
    perror("test_zalloc: calloc failed");
    abort();
  }

  return ptr;
}

void test_randinit(void *buf, size_t len)
{
  uint8_t *b = buf;
  size_t i = 0;

  for (i = 0; i < len; i++)
    b[i] = rand();
}

void test_error(const char *msg)
{
  fprintf(stderr, "Error: %s\n", msg);
  exit(1);
}

void test_assert(const char *msg, int cond)
{
  if (cond)
    return;

  fprintf(stderr, "Assertion failed: %s\n", msg);
  exit(1);
}

int test_subcase(const char *name, void (*run)(void *), void *param)
{
  pid_t pid, p;
  int status;

  printf("Subcase: %s ...\n", name);

  pid = fork();
  if (!pid) {
    /* in child */
    run(param);
    exit(0);
  } else if (pid < 0) {
    perror("fork failed");
    return -1;
  } else {
    p = waitpid(pid, &status, 0);
    if (p == pid) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("  [passed]\n");
        return 0;
      } else {
        printf("  [failed] %d %d\n", WIFEXITED(status), WEXITSTATUS(status));
        return -1;
      }
    } else {
      perror("test_subcase: waitpid failed");
      return -1;
    }
  }
}
