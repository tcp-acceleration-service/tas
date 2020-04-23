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

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <pthread.h>

#include <tas_ll.h>


static int run_child(int (*tas_entry)(void *), int (*linux_entry)(void *),
    void *data);
static int setgroups_deny(void);
static int set_idmap(const char *path, int new_id, int env_id);

/* run a command blockingly, return value is exit code of command. */
static int simple_cmd(const char *path, char * const args[])
{
  int nret;
  pid_t pid, npid;

  pid = fork();
  if (pid == 0) {
    /* in child */
    nret = execvp(path, args);
    perror("simple_cmd: execvp failed");
    exit(1);
  } else if (pid < 0) {
    perror("fork failed");
    return -1;
  } else {
    npid = waitpid(pid, &nret, 0);
    if (npid < 0) {
      perror("waitpid failed");
      return 1;
    }

    if (WIFEXITED(nret)) {
      return WEXITSTATUS(nret);
    } else {
      return 1;
    }
  }
}

/* start TAS and wait for it to be ready, returns PID or < 0 on error. */
static pid_t start_tas(void)
{
  int ready_fd, ret;
  pid_t pid;
  char readyfdopt[32];
  uint64_t x = 0;

  /* create event notification fd */
  if ((ready_fd = eventfd(0, 0)) < 0) {
    perror("eventfd for ready fd failed");
    return -1;
  }

  sprintf(readyfdopt, "--ready-fd=%d", ready_fd);

  /* fork off tas */
  pid = fork();
  if (pid == 0) {
    /* in child */
    execl("tas/tas", "--fp-cores-max=1", "--fp-no-ints", "--fp-no-xsumoffload",
        "--fp-no-autoscale", "--fp-no-hugepages", "--dpdk-extra=--vdev",
        "--dpdk-extra=eth_tap0,iface=vethtas1",
        "--dpdk-extra=--no-shconf", "--dpdk-extra=--no-huge",
        "--ip-addr=192.168.1.1/24", readyfdopt, NULL);

    perror("exec failed");
    exit(1);
  } else if (pid < 0) {
    /* fork failed */
    return -1;
  }

  /* wait for TAS to be ready */
  if (read(ready_fd, &x, sizeof(x)) < 0) {
    perror("read from readyfd failed");
    goto out_error;
  } else if (x != 1) {
    fprintf(stderr, "read unexpected value from ready fd\n");
    goto out_error;
  }

  return pid;

out_error:
  kill(pid, SIGTERM);
  waitpid(pid, &ret, 0);
  return -1;
}

int full_testcase(int (*tas_entry)(void *), int (*linux_entry)(void *),
    void *data)
{
  int ret = 0, nret;
  int env_uid, env_gid;
  pid_t pid, npid;

  pid = fork();
  if (pid == 0) {
    /* in child */
    env_uid = getuid();
    env_gid = getgid();

    /* in child */
    if (unshare(CLONE_NEWUSER | CLONE_NEWIPC | CLONE_NEWNS | CLONE_NEWNET)
        != 0)
    {
      perror("unshare user namespace failed");
      exit(EXIT_FAILURE);
    }

    if (setgroups_deny() != 0) {
      perror("setgroups deny failed");
      exit(EXIT_FAILURE);
    }

    if (set_idmap("/proc/self/uid_map", 0, env_uid) != 0) {
      perror("map uid failed");
      exit(EXIT_FAILURE);
    }

    if (set_idmap("/proc/self/gid_map", 0, env_gid) != 0) {
      perror("map gid failed");
      exit(EXIT_FAILURE);
    }

    exit(run_child(tas_entry, linux_entry, data));
  } else if (pid > 0) {
    /* in parent */
    npid = waitpid(pid, &nret, 0);
    if (npid < 0) {
      perror("waitpid failed");
      return 1;
    }

    if (WIFEXITED(nret)) {
      ret = WEXITSTATUS(nret);
    } else {
      ret = 1;
    }
  } else {
    ret = 1;
  }

  return ret;
}

struct thread_wrapper {
  void *param;
  int (*fun)(void *);
  volatile int ret;
};

static void *thread_wrapper(void *data)
{
  struct thread_wrapper *tw = data;
  tw->ret = tw->fun(tw->param);
  return NULL;
}

static int run_child(int (*tas_entry)(void *), int (*linux_entry)(void *),
    void *data)
{
  pid_t pid, npid, tas_pid;
  int ret, nret;
  pthread_t t_l, t_t;
  struct thread_wrapper tw_l, tw_t;
  static char *ip_addr_cmd[] = {"ip", "addr", "add", "192.168.1.2/24", "dev",
    "vethtas1", NULL };
  static char *ip_up_cmd[] = {"ip", "link", "set", "vethtas1", "up", NULL };

  umask(0);
  pid = fork();
  if (pid == 0) {
    /* in child */

    if (mount("tmpfs", "/dev/shm", "tmpfs", 0, "") != 0) {
      perror("mounting /dev/shm failed");
      return 1;
    }

    umask(0022);

    /* start tas */
    if ((tas_pid = start_tas()) < 0) {
      fprintf(stderr, "start_tas failed\n");
      return 1;
    }

    /* set ip address for TAS interface and bring it up */
    if (simple_cmd(ip_addr_cmd[0], ip_addr_cmd) != 0) {
      fprintf(stderr, "ip addr failed\n");
      return 1;
    }
    if (simple_cmd(ip_up_cmd[0], ip_up_cmd) != 0) {
      fprintf(stderr, "ip set up failed\n");
      return 1;
    }

    /* start linux and tas test threads */
    tw_l.param = data;
    tw_l.fun = linux_entry;
    tw_t.param = data;
    tw_t.fun = tas_entry;
    if (pthread_create(&t_l, NULL, thread_wrapper, &tw_l) != 0) {
      perror("pthread create linux thread failed");
      abort();
    }
    if (pthread_create(&t_t, NULL, thread_wrapper, &tw_t) != 0) {
      perror("pthread create tas thread failed");
      abort();
    }

    /* wait for linux and tas threads to return */
    if (pthread_join(t_l, NULL) != 0) {
      perror("pthread join linux thread failed");
      abort();
    }
    if (pthread_join(t_t, NULL) != 0) {
      perror("pthread join tas thread failed");
      abort();
    }

    /* send sigterm to TAS and wait for it to terminate */
    kill(tas_pid, SIGTERM);

    /* wait for tas to terminate */
    npid = waitpid(tas_pid, &nret, 0);
    if (npid < 0) {
      perror("waitpid failed");
      return 1;
    }

    return (tw_t.ret == 0 && tw_l.ret == 0 ? 0 : -1);
  } else if (pid > 0) {
    /* in parent */
    npid = waitpid(pid, &ret, 0);
    if (npid < 0) {
      perror("waitpid failed");
      return 1;
    }

    if (WIFEXITED(ret)) {
      return WEXITSTATUS(ret);
    } else {
      return 1;
    }
  } else {
    perror("fork failed");
  }

  return 0;
}

static int setgroups_deny(void)
{
    int fd;
    ssize_t ret, len;
    const char *deny_str = "deny";

    if ((fd = open("/proc/self/setgroups", O_WRONLY)) == -1) {
        perror("setgroups_deny: open failed");
        return -1;
    }

    len = strlen(deny_str);
    ret = write(fd, deny_str, len);
    close(fd);
    if (ret < 0) {
        perror("setgroups_deny: write failed");
        return -1;
    } else if (ret != len) {
        perror("setgroups_deny: partial write");
        return -1;
    }

    return 0;
}

static int set_idmap(const char *path, int new_id, int env_id)
{
    int fd;
    ssize_t ret, len;
    char str[64];

    if (snprintf(str, sizeof(str), "%u %u 1", new_id, env_id) >=
        (ssize_t) sizeof(str))
    {
        perror("set_idmap: buffer too small");
        return -1;
    }
    len = strlen(str);

    if ((fd = open(path, O_WRONLY)) == -1) {
        perror("set_idmap: open failed");
        return -1;
    }

    ret = write(fd, str, len);
    close(fd);
    if (ret < 0) {
        perror("set_idmap: write failed");
        return -1;
    } else if (ret != len) {
        perror("set_idmap: partial write");
        return -1;
    }

    return 0;
}
