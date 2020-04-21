#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>

#include "fulltest.h"

struct child {
  char *cmd;
  pid_t pid;
  int status;
  bool ld_pre;
  bool wait;

  struct child *next;
};

static struct child *children = NULL;
static struct child *children_last = NULL;
static int timeout = -1;
static int child_delay = 0;
static const char *libpath = "lib/libtas_interpose.so";

static void child_add(struct child *c)
{
  c->next = NULL;
  if (children == NULL) {
    children = children_last = c;
  } else {
    children_last->next = c;
    children_last = c;
  }
}

static int child_start(struct child *c)
{
  pid_t pid;
  wordexp_t we;

  pid = fork();
  if (pid > 0) {
    /* in parent */
    c->pid = pid;
  } else if (pid == 0) {
    /* in child */

    if (setenv("TAS_IP", "192.168.1.1", 1) != 0 ||
        setenv("LINUX_IP", "192.168.1.2", 1) != 0)
    {
      perror("child_start (child): setenv IP failed");
      exit(EXIT_FAILURE);
    }

    if (c->ld_pre && setenv("LD_PRELOAD", libpath, 1) != 0) {
      perror("child_start (child): setenv LD_PRELOAD failed");
      exit(EXIT_FAILURE);
    }

    /* parse cmd string */
    if (wordexp(c->cmd, &we, WRDE_NOCMD) != 0) {
      perror("child_start (child): wordexp failed");
      exit(EXIT_FAILURE);
    }

    execvp(we.we_wordv[0], we.we_wordv);
    perror("child_start (child): execvp failed");
    exit(EXIT_FAILURE);
  } else {
    perror("child_start: fork failed");
    return -1;
  }

  return 0;
}

struct child *child_wait(void)
{
  pid_t pid;
  int status;
  struct child *c;

  pid = wait(&status);
  if (pid < 0) {
    perror("wait failed\n");
    return NULL;
  }

  /* look for child with this pid */
  for (c = children; c != NULL; c = c->next) {
    if (c->pid == pid) {
      c->status = status;
      c->pid = -1;
      return c;
    }
  }

  return NULL;
}

static int thread_run(void *data)
{
  struct child *c;
  int failed = 0;
  unsigned children_alive = 0, children_alive_wait = 0;

  /* start children */
  for (c = children; c != NULL && !failed; c = c->next) {
    if (child_start(c) == 0) {
      children_alive++;
      if (c->wait)
        children_alive_wait++;
    } else {
      failed = 1;
      goto cleanup;
    }

    /* sleep for a bit, if requested */
    if (child_delay > 0)
      usleep((useconds_t) child_delay * 1000);
  }

  /* wait for children that are marked as to wait for */
  while (children_alive_wait > 0) {
    c = child_wait();
    if (c != NULL) {
      children_alive--;
      if (c->wait)
        children_alive_wait--;
    } else {
      fprintf(stderr, "thread_run: waiting for active child failed\n");
    }
  }

cleanup:
  /* kill all children that are still alive */
  for (c = children; c != NULL; c = c->next) {
    if (c->pid == -1)
      continue;

    if (kill(c->pid, SIGTERM) != 0)
      perror("thread_run: killing child failed");
  }

  /* wait for all children to terminate */
  while (children_alive_wait > 0) {
    c = child_wait();
    if (c != NULL) {
      children_alive--;
    } else {
      fprintf(stderr, "thread_run: waiting for active child failed\n");
    }
  }

  /* check status of all children now */
  for (c = children; c != NULL && !failed; c = c->next) {
    /* skip children we don't wait for */
    if (!c->wait)
      continue;

    if (!WIFEXITED(c->status) || WEXITSTATUS(c->status) != 0) {
      fprintf(stderr, "child %d '%s' failed: st=%d\n", c->pid, c->cmd,
          c->status);
      failed = 1;
    }
  }

  return (failed ? -1 : 0);
}

static int dummy_run(void *data)
{
  return 0;
}


static int parse_args(int argc, char *argv[])
{
  int opt;
  struct child *c;

  while ((opt = getopt(argc, argv, "p:P:c:C:d:t:")) != -1) {
    switch (opt) {
      case 'p': /* fallthrough */
      case 'P': /* fallthrough */
      case 'c': /* fallthrough */
      case 'C': /* fallthrough */
        if ((c = calloc(1, sizeof(*c))) == NULL) {
          perror("alloc child struct failed");
          return -1;
        }

        if ((c->cmd = strdup(optarg)) == NULL) {
          perror("alloc cmd copy failed");
          free(c);
          return -1;
        }

        c->status = -1;
        c->pid = -1;
        c->ld_pre = (opt == 'p' || opt == 'P');
        c->wait = (opt == 'p' || opt == 'c');

        child_add(c);
        break;

      case 'd':
        child_delay = atoi(optarg);
        break;

      case 't':
        timeout = atoi(optarg);
        break;

      default:
        fprintf(stderr, "Usage\n");
        return -1;
    }
  }

  if (optind < argc) {
    fprintf(stderr, "Usage\n");
    return -1;
  }

  if (c == NULL) {
    fprintf(stderr, "No children specified\n");
    return -1;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  if (parse_args(argc, argv) != 0) {
    return 1;
  }

  return full_testcase(thread_run, dummy_run, NULL);
}
