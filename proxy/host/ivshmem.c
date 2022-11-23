#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <errno.h>

#include "../proxy.h"
#include "../channel.h"
#include "../../lib/tas/internal.h"
#include "ivshmem.h"

/* Epoll that subscribes to:
   - uxfd
   - fd from each guest proxy connection
*/
static int epfd = -1;
/* Unix socket that connects host proxy to guest proxy */
static int uxfd = -1;
/* Epoll for contexsts */
static int ctx_epfd = -1;
/* ID of next virtual machine to connect */
static int next_id = 0;
/* List of VMs */
static struct v_machine vms[MAX_VMS];

static int ivshmem_notify_guest(int fd);

static int ivshmem_uxsocket_init();
static int ivshmem_uxsocket_poll();

static int ivshmem_uxsocket_handle_newconn();
static int ivshmem_uxsocket_handle_notif();
static int ivshmem_uxsocket_handle_error();
static int ivshmem_uxsocket_handle_msg();
static int ivshmem_handle_tasinforeq_msg(struct v_machine *vm);

static int ivshmem_uxsocket_send_int(int fd, int64_t i);
static int ivshmem_uxsocket_sendfd(int uxfd, int fd, int64_t i);

static int ivshmem_ctxs_poll();
static int ivshmem_handle_ctx_req(struct v_machine *vm, 
        struct context_req_msg *msg);

int ivshmem_init()
{
    struct epoll_event ev;

    /* Create unix socket for comm between guest and host proxies */
    if ((uxfd = ivshmem_uxsocket_init()) < 0)
    {
        fprintf(stderr,"ivshmem_init: failed to init unix socket.\n");
        return -1;
    }

    /* Create epoll used to subscribe to events */
    if ((epfd = epoll_create1(0)) < -1) 
    {
        fprintf(stderr, "ivshmem_init: epoll_create1 for epfd failed.\n"); 
        goto close_uxfd;
    }

    /* Create epoll to listen to subscribe to contexts */
    if ((ctx_epfd = epoll_create1(0)) == -1) 
    {
        fprintf(stderr, "ivshmem_init: epoll_create1 for ctx epfd failed.");
        goto close_epfd;
    }

    /* Add unix socket to interest list */
    ev.events = EPOLLIN;
    ev.data.ptr = EP_LISTEN;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, uxfd, &ev) != 0) 
    {
        fprintf(stderr, "ivshmem_init: epoll_ctl listen failed.\n"); 
        goto close_ctx_epfd;
    }

    return 0;

close_ctx_epfd:
    close(ctx_epfd);
close_epfd:
    close(epfd);
close_uxfd:
    close(uxfd);

    return -1;
}

int ivshmem_poll() 
{
    if (ivshmem_uxsocket_poll() != 0)
    {
        fprintf(stderr, "ivshmem_poll: failed to poll uxsocket.\n");
        return -1;
    }

    if (ivshmem_ctxs_poll() != 0) 
    {
        fprintf(stderr, "ivshmem_poll: failed to poll ctxs fds.\n");
        return -1;
    }

    return 0;
}

int ivshmem_notify_guest(int fd)
{
  int ret;
  uint64_t buf = 1;
  
  ret = write(fd, &buf, sizeof(uint64_t));
  if (ret < sizeof(uint64_t))
  {
    fprintf(stderr, "ivshmem_notify_guest: failed to notify host.\n");
    return -1;
  }
  
  return 0;
}

/* Clears the interrupt status register. If register is not cleared
   we keep receiving interrupt events from epoll. */
int ivshmem_drain_evfd(int fd) 
{
  uint8_t buf[8];
  read(fd, buf, 8);
  return 0;
}

/*****************************************************************************/
/* Unix Socket */

static int ivshmem_uxsocket_init()
{
    int fd;
    struct sockaddr_un saun;

    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
        {
            fprintf(stderr, "ivshmem_uxsocket_init: open socket failed.\n");
            return -1;
        }

    /* Create sockaddr */
    memset(&saun, 0, sizeof(saun));
    saun.sun_family = AF_UNIX;
    memcpy(saun.sun_path, IVSHMEM_SOCK_PATH, sizeof(IVSHMEM_SOCK_PATH));

    /* Unlink deletes socket when program exits */
    unlink(IVSHMEM_SOCK_PATH);

    if (bind(fd, (struct sockaddr *) &saun, sizeof(saun)) != 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_init: bind failed.\n");
        goto close_fd;
    }

    if (listen(fd, 5))
    {
        fprintf(stderr, "ivshmem_uxsocket_init: failed to listen to"
                "unix socket.\n");
        goto close_fd;
    }

    if (chmod(IVSHMEM_SOCK_PATH, 0x666))
    {
        fprintf(stderr, "ivshmem_uxsocket_init: failed setting" 
                "socket permissions.\n");
        goto close_fd;
    }

    return fd;  

close_fd:
    close(fd);
    return -1;
}

static int ivshmem_uxsocket_poll()
{
    int i, n;
    struct v_machine *vm;
    struct epoll_event evs[32];
    struct epoll_event ev;

    if ((n = epoll_wait(epfd, evs, 32, 0)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_poll: epoll_wait failed.\n");
        return -1;
    }

    /* Handle each event from epoll */
    for (i = 0; i < n; i++)
    {
        ev = evs[i];
        if (ev.data.ptr == EP_LISTEN)
        {
            ivshmem_uxsocket_handle_newconn();
        }
        else if (ev.data.ptr == EP_NOTIFY)
        {
            ivshmem_uxsocket_handle_notif();
        }
        else if (evs[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
        {
            ivshmem_uxsocket_handle_error();
        }
        else if (evs[i].events & EPOLLIN)
        {
            vm = ev.data.ptr; 
            ivshmem_drain_evfd(vm->nfd);
            ivshmem_uxsocket_handle_msg(vm);
        }
    }

    return 0;
}

/* Accepts connection when a qemu vm starts and uses the
   uxsocket to set up the shared memory region */
static int ivshmem_uxsocket_handle_newconn()
{   
    int ret;
    int cfd, ifd, nfd;
    void *tx_addr, *rx_addr;
    struct hello_msg h_msg;
    struct epoll_event ev;
    struct channel *chan;

    int64_t version = IVSHMEM_PROTOCOL_VERSION;
    uint64_t hostid = HOST_PEERID;

    /* Return error if max number of VMs has been reached */
    if (next_id > MAX_VMS)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: max vms reached.\n");
        return -1;
    }

    /* Accept connection from qemu ivshmem */
    if ((cfd = accept(uxfd, NULL, NULL)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: accept failed.\n");
        return -1;
    }

    /* Send protocol version as required by qemu ivshmem */
    ret = ivshmem_uxsocket_send_int(cfd, version);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to send "
                "protocol version.\n");
        goto close_cfd;
    }

    /* Send vm id to qemu */
    ret = ivshmem_uxsocket_send_int(cfd, next_id);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to send vm id.\n");
        goto close_cfd;
    }

    /* Send shared memory fd to qemu */
    ret = ivshmem_uxsocket_sendfd(cfd, flexnic_shmfd, -1);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to send shm fd.\n");
        goto close_cfd;
    }

    /* Create and send fd so that vm can interrupt host */
    if ((nfd = eventfd(0, EFD_NONBLOCK)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_acccept: failed to create"
                "notify fd.\n");
        goto close_cfd;
    }

    /* Ivshmem protocol requires to send host id
       with the notify fd */
    ret = ivshmem_uxsocket_sendfd(cfd, nfd, hostid);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to send "
                "notify fd.\n");
        goto close_ifd;
        
    }

    /* Create and send fd so that host can interrupt vm */
    if ((ifd = eventfd(0, EFD_NONBLOCK)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to create "
                "interrupt fd.\n");
        goto close_nfd;
    }

    ret = ivshmem_uxsocket_sendfd(cfd, ifd, next_id);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to send "
                "interrupt fd.\n");
        goto close_ifd;
    }

    /* Add connection fd to the epoll interest list */
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to add "
                "cfd to epfd.\n");
        goto close_ifd;
    }

    /* Add notify fd to the epoll interest list */
    ev.events = EPOLLIN;
    ev.data.ptr = &vms[next_id];
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, nfd, &ev) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to add "
                "nfd to epfd.\n");
        goto close_ifd;
    }

    /* Init channel in shared memory */
    tx_addr = flexnic_mem + CHAN_OFFSET + (next_id * CHAN_SIZE * 2)
            + CHAN_SIZE;
    rx_addr = flexnic_mem + CHAN_OFFSET + (next_id * CHAN_SIZE * 2);
    chan = channel_init(tx_addr, rx_addr, CHAN_SIZE);
    if (chan == NULL)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to init chan.\n");
        goto close_ifd;
    }

    /* Write number of cores to channel for guest proxy to receive */
    h_msg.msg_type = MSG_TYPE_HELLO;
    h_msg.n_cores = flexnic_info->cores_num;

    ret = channel_write(chan, &h_msg, sizeof(struct hello_msg));
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_newconn: failed to send number "
                "of cores to guest.\n");
        goto close_ifd;
    }

    vms[next_id].ifd = ifd;
    vms[next_id].nfd = nfd;
    vms[next_id].id = next_id;
    vms[next_id].chan = chan;
    fprintf(stdout, "Connected VM=%d\n", next_id);
    
    next_id++;

    return 0;

close_ifd:
    close(ifd);
close_nfd:
    close(nfd);
close_cfd:
    close(cfd);

    return -1;
}

static int ivshmem_uxsocket_handle_notif()
{
    return 0;
}

static int ivshmem_uxsocket_handle_error()
{
    fprintf(stderr, "ivshmem_uxsocket_error: epoll_wait error.\n");
    return 0;
}

static int ivshmem_uxsocket_handle_msg(struct v_machine *vm)
{
    int ret;
    void *msg;
    uint8_t msg_type;
    size_t msg_size;

    if (vm == NULL)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_msg: vm is NULL.\n");
        return -1;
    }

    msg_type = channel_get_msg_type(vm->chan);
    msg_size = channel_get_type_size(msg_type);
    msg = malloc(msg_size);

    ret = channel_read(vm->chan, msg, msg_size);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_handle_msg: channel read failed.\n");
        return -1;
    }

    switch(msg_type)
    {
        case MSG_TYPE_TASINFO_REQ:
            /* Send tas info struct to guest */
            ivshmem_handle_tasinforeq_msg(vm);
            ivshmem_notify_guest(vm->ifd);
            break;
        case MSG_TYPE_CONTEXT_REQ:
            ivshmem_handle_ctx_req(vm, msg);
            ivshmem_notify_guest(vm->ifd);
            break;
        default:
            fprintf(stderr, "ivshmem_uxsocket_handle_msg: unknown message.\n");
    }

    return 0;
}

/* Handles tasinfo request */
static int ivshmem_handle_tasinforeq_msg(struct v_machine *vm)
{
    int ret;
    struct tasinfo_res_msg *msg;

    msg = (struct tasinfo_res_msg *) malloc(sizeof(struct tasinfo_res_msg));

    if (msg == NULL)
    {
        fprintf(stderr, "ivshmem_handle_tasinforeq_msg: "
                "failed to allocate tasinfo_res_msg.\n");
        return -1;
    }

    msg->msg_type = MSG_TYPE_TASINFO_RES;
    if (memcpy(msg->flexnic_info, flexnic_info, FLEXNIC_INFO_BYTES) == NULL)
    {
        fprintf(stderr, "ivshmem_handle_tasinforeq_msg: "
                "failed to cpy flexnic_info to msg.\n");
        goto free_msg;
    }

    ret = channel_write(vm->chan, msg, sizeof(struct tasinfo_res_msg));
    if (ret < sizeof(struct tasinfo_res_msg))
    {
        fprintf(stderr, "ivshmem_handle_tasinforeq_msg: "
                "failed to write to channel.\n");
        goto free_msg;
    }

    return 0;

free_msg:
    free(msg);

    return -1;

}

static int ivshmem_uxsocket_send_int(int fd, int64_t i)
{
    int n;

    struct iovec iov = {
        .iov_base = &i,
        .iov_len = sizeof(i),
    };

    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0,
    };

    if ((n = sendmsg(fd, &msg, 0)) != sizeof(int64_t)) 
    {
        fprintf(stderr, "ivshmem_uxsocket_send: failed to send msg.\n");

        return -1;
    }

    return n;
}

static int ivshmem_uxsocket_sendfd(int uxfd, int fd, int64_t i)
{
    int n;
    struct cmsghdr *chdr;

    /* Need to pass at least one byte of data to send control data */
    struct iovec iov = {
        .iov_base = &i,
        .iov_len = sizeof(i),
    };

    /* Allocate a char array but use a union to ensure that it
       is alligned properly */
    union {
        char buf[CMSG_SPACE(sizeof(fd))];
        struct cmsghdr align;
    } cmsg;
    memset(&cmsg, 0, sizeof(cmsg));

    /* Add control data (file descriptor) to msg */
    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = &cmsg,
        .msg_controllen = sizeof(cmsg),
        .msg_flags = 0,
    };

    /* Set message header to describe ancillary data */
    chdr = CMSG_FIRSTHDR(&msg);
    chdr->cmsg_level = SOL_SOCKET;
    chdr->cmsg_type = SCM_RIGHTS;
    chdr->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(chdr), &fd, sizeof(fd));

    if ((n = sendmsg(uxfd, &msg, 0)) != sizeof(i)) 
    {
        fprintf(stderr, "ivshmem_uxsocket_sendfd: failed to send msg.\n");
        return -1;
    }

    return n;
}


/*****************************************************************************/
/* Contexts */

static int ivshmem_ctxs_poll() 
{
    int i, n, ret;
    struct vmcontext_req *vctx;
    struct _msg;
    struct epoll_event evs[32];
    struct vpoke_msg msg;

    n = epoll_wait(ctx_epfd, evs, 32, 0);
    if (n < 0) 
    {
        fprintf(stderr, "ivshmem_poll: epoll_wait failed");
        if (errno == EINTR) 
        {
            return -1; 
        }
        
        return -1;
    } 
    else if (n > 0) 
    {
        for (i = 0; i < n; i++) 
        {
            vctx = evs[i].data.ptr;
            ivshmem_drain_evfd(vctx->ctx->evfd);
            
            msg.msg_type = MSG_TYPE_VPOKE;
            msg.vfd = vctx->vfd;
            ret = channel_write(vctx->vm->chan, &msg, sizeof(struct vpoke_msg));
            if (ret < sizeof(struct vpoke_msg))
            {
                fprintf(stderr, "ivshmem_ctxs_poll: failed to write poke msg.\n");
                return -1;
            }            
            ivshmem_notify_guest(vctx->vm->ifd);
        }
    }

    return 0;
}

static int ivshmem_handle_ctx_req(struct v_machine *vm, 
        struct context_req_msg *msg) 
{
  int ret;
  struct context_res_msg res_msg;
  struct flextcp_context *ctx;
  struct vmcontext_req *vctx;
  uint8_t resp_buf[CTX_RESP_MAX_SIZE];
  struct epoll_event ev;

  /* allocate a flextcp_context and set it up */
  ctx = malloc(sizeof(struct flextcp_context));
  memset(resp_buf, 0, sizeof(resp_buf));
  
  if (ctx == NULL) 
  {
    fprintf(stderr, "ivshmem_handle_ctx_req: " 
        "failed to allocate memory for context request.\n");
    return -1;
  }

  if (flextcp_context_create(ctx, res_msg.resp, &res_msg.resp_size) == -1) 
  {
    fprintf(stderr, "ivshmem_handle_ctxreq: "
            "failed to create context request.");
    return -1;
  }

  /* allocate memory to keep track of different context queues from 
   * different applications on the same vm */
  vctx = malloc(sizeof(struct vmcontext_req));
  if (vctx == NULL) 
  {
    fprintf(stderr, "ivshmem_handle_ctxreq: failed to allocate memory for "
        "vm_context_req" );
    return -1;
  }

  vctx->ctx = ctx;
  vctx->cfd = msg->cfd;
  vctx->vfd = msg->vfd;
  vctx->app_id = msg->app_id;
  vctx->next = vm->ctxs;
  vctx->vm = vm;
  vm->ctxs = vctx;

  res_msg.msg_type = MSG_TYPE_CONTEXT_RES,
  res_msg.vfd = msg->vfd,
  res_msg.app_id = msg->app_id,

  ret = channel_write(vm->chan, &res_msg, sizeof(struct context_res_msg));
  if (ret < sizeof(struct context_res_msg))
  {
    fprintf(stderr, "ivshmem_handle_ctxreq: failed to send ctx res.\n");
    return -1;
  }

  /* add vctx to context request epoll */
  ev.events = EPOLLIN;
  ev.data.ptr = vctx;
  if (epoll_ctl(ctx_epfd, EPOLL_CTL_ADD, vctx->ctx->evfd, &ev) != 0) 
  {
    fprintf(stderr, "ivshmem_handle_ctxreq: epoll_ctl listen failed."); 
    return -1;
  }

  return 0;
}