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

#include <fcntl.h>

#include <tas_pxy.h>
#include "../proxy.h"
#include "../channel.h"
#include "ivshmem.h"

static int notify_guest(int fd);

static int uxsocket_init();
static int uxsocket_poll();
static int uxsocket_accept();
static int uxsocket_handle_notif();
static int uxsocket_handle_error();
static int uxsocket_send_int(int fd, int64_t i);
static int uxsocket_sendfd(int uxfd, int fd, int64_t i);

static int app_ctxs_poll(struct host_proxy *pxy);

static int channel_poll();
static int channel_poll_vm(struct host_proxy *pxy,
        struct v_machine *vm);
static int channel_handle_tasinforeq_msg(struct v_machine *vm);
static int channel_handle_newapp(struct host_proxy *pxy, 
        struct v_machine *vm, struct newapp_req_msg *req_msg);
static int channel_handle_ctx_req(struct host_proxy *pxy,
        struct v_machine *vm, struct context_req_msg *msg);
static void channel_handle_poke_tas_kernel(struct v_machine *vm, 
        struct poke_tas_kernel_msg *msg);
static void channel_handle_poke_tas_core(struct v_machine *vm, 
        struct poke_tas_core_msg *msg);



int ivshmem_init(struct host_proxy *pxy)
{
    struct epoll_event ev;

    /* Create unix socket for comm between guest and host proxies */
    if ((pxy->uxfd = uxsocket_init()) < 0)
    {
        fprintf(stderr,"ivshmem_init: failed to init unix socket.\n");
        return -1;
    }

    /* Create epoll used to subscribe to events */
    if ((pxy->epfd = epoll_create1(0)) < -1) 
    {
        fprintf(stderr, "ivshmem_init: epoll_create1 for epfd failed.\n"); 
        goto close_uxfd;
    }

    /* Create epoll to subscribe to interrupts from guests */
    if ((pxy->chan_epfd = epoll_create1(0)) < -1) 
    {
        fprintf(stderr, "ivshmem_init: epoll_create1 for chan_epfd failed.\n"); 
        goto close_epfd;
    }

    /* Create epoll to subscribe to contexts */
    if ((pxy->ctx_epfd = epoll_create1(0)) == -1) 
    {
        fprintf(stderr, "ivshmem_init: epoll_create1 for ctx epfd failed.");
        goto close_chan_epfd;
    }

    /* Create epoll to control blocking */
    if ((pxy->block_epfd = epoll_create1(0)) == -1)
    {
        fprintf(stderr, "ivshmem_init: epoll_create1 for block epfd failed.");
        goto close_ctx_epfd;
    }

    /* Add unix socket to interest list */
    ev.events = EPOLLIN;
    ev.data.ptr = EP_LISTEN;
    if (epoll_ctl(pxy->epfd, EPOLL_CTL_ADD, pxy->uxfd, &ev) != 0) 
    {
        fprintf(stderr, "ivshmem_init: epoll_ctl listen failed.\n"); 
        goto close_block_epfd;
    }

    /* Add unix socket to blocking epoll */
    if (epoll_ctl(pxy->block_epfd, EPOLL_CTL_ADD, pxy->uxfd, &ev) != 0)
    {
        fprintf(stderr, "ivshmem_init: epoll_ctl for block failed.\n"); 
        goto close_block_epfd;
    }
    
    /* Initialize all ids to 0 */
    memset(pxy->next_app_id, 0, sizeof(pxy->next_app_id));

    return 0;

close_block_epfd:
    close(pxy->block_epfd);
close_ctx_epfd:
    close(pxy->ctx_epfd);
close_chan_epfd:
    close(pxy->chan_epfd);
close_epfd:
    close(pxy->epfd);
close_uxfd:
    close(pxy->uxfd);

    return -1;
}

int ivshmem_poll(struct host_proxy *pxy) 
{
    int ret, n = 0;


    if ((ret = uxsocket_poll(pxy)) < 0)
    {
        fprintf(stderr, "ivshmem_poll: failed to poll uxsocket.\n");
        return -1;
    }
    n += ret;

    if ((ret = channel_poll(pxy)) < 0)
    {
        fprintf(stderr, "ivshmem_poll: failed to poll proxy channel.\n");
        return -1;
    }
    n += ret;

    if ((ret = app_ctxs_poll(pxy)) < 0) 
    {
        fprintf(stderr, "ivshmem_poll: failed to poll ctxs fds.\n");
        return -1;
    }
    n += ret;

    return n;
}

int notify_guest(int fd)
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
uint64_t ivshmem_drain_evfd(int fd) 
{
  int ret;
  uint64_t buf;
  ret = read(fd, &buf, sizeof(uint64_t));
  if (ret == 0)
  {
    fprintf(stderr, "ivshmem_drain_evfd: failed to drain evfd.\n");
    return -1;
  }

  return buf;
}

/*****************************************************************************/
/* Unix Socket */

static int uxsocket_init()
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

static int uxsocket_poll(struct host_proxy *pxy)
{
    int i, n;
    struct epoll_event evs[32];
    struct epoll_event ev;

    if ((n = epoll_wait(pxy->epfd, evs, 32, 0)) < 0)
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
            uxsocket_accept(pxy);
        }
        else if (ev.data.ptr == EP_NOTIFY)
        {
            uxsocket_handle_notif();
        }
        else if (evs[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
        {
            uxsocket_handle_error();
        }
    }

    return n;
}

/* Accepts connection when a qemu vm starts and uses the
   uxsocket to set up the shared memory region */
static int uxsocket_accept(struct host_proxy *pxy)
{   
    int ret;
    int cfd, ifd, nfd;
    void *tx_addr, *rx_addr;
    struct hello_msg h_msg;
    struct epoll_event ev;
    struct channel *chan;

    int64_t version = IVSHMEM_PROTOCOL_VERSION;
    uint64_t hostid = HOST_PEERID;

    /* Init to 0 to prevent invalid argument errors from epoll ctl */
    memset(&ev, 0, sizeof(ev));

    /* Return error if max number of VMs has been reached */
    if (pxy->next_vm_id > MAX_VMS)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: max vms reached.\n");
        return -1;
    }

    /* Accept connection from qemu ivshmem */
    if ((cfd = accept(pxy->uxfd, NULL, NULL)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: accept failed.\n");
        return -1;
    }

    /* Send protocol version as required by qemu ivshmem */
    ret = uxsocket_send_int(cfd, version);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send "
                "protocol version.\n");
        goto close_cfd;
    }

    /* Send vm id to qemu */
    ret = uxsocket_send_int(cfd, pxy->next_vm_id);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send vm id.\n");
        goto close_cfd;
    }

    /* Send shared memory fd to qemu */
    ret = uxsocket_sendfd(cfd, flexnic_shmfds_pxy[pxy->next_vm_id], -1);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send shm fd.\n");
        goto close_cfd;
    }

    /* Create and send fd so that vm can interrupt host */
    if ((nfd = eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_acccept: failed to create"
                "notify fd.\n");
        goto close_cfd;
    }

    /* Ivshmem protocol requires to send host id
       with the notify fd */
    ret = uxsocket_sendfd(cfd, nfd, hostid);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send "
                "notify fd.\n");
        goto close_nfd;
        
    }

    /* Create and send eventfd so that host can interrupt vm */
    if ((ifd = eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to create "
                "interrupt fd.\n");
        goto close_nfd;
    }

    ret = uxsocket_sendfd(cfd, ifd, pxy->next_vm_id);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send "
                "interrupt fd.\n");
        goto close_ifd;
    }

    if (epoll_ctl(pxy->epfd, EPOLL_CTL_ADD, cfd, &ev) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to add "
                "cfd to epfd.\n");
        goto close_ifd;
    }

    if (epoll_ctl(pxy->block_epfd, EPOLL_CTL_ADD, cfd, &ev) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to add "
                "cfd to block_epfd.\n");
        goto close_ifd;
    }

    /* Add notify fd to the chan epoll interest list */
    ev.events = EPOLLIN;
    ev.data.ptr = &pxy->vms[pxy->next_vm_id];
    if (epoll_ctl(pxy->chan_epfd, EPOLL_CTL_ADD, nfd, &ev) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to add "
                "nfd to chan epfd.\n");
        goto close_ifd;
    }

    /* Add notify fd to blocking epoll */
    if (epoll_ctl(pxy->block_epfd, EPOLL_CTL_ADD, nfd, &ev) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to add "
                "nfd to block epfd.\n");
        goto close_ifd;
    }

    /* Init channel in shared memory */
    tx_addr = flexnic_mem_pxy[pxy->next_vm_id] + CHAN_OFFSET + CHAN_SIZE;
    rx_addr = flexnic_mem_pxy[pxy->next_vm_id] + CHAN_OFFSET;
    chan = channel_init(tx_addr, rx_addr, CHAN_SIZE);
    if (chan == NULL)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to init chan.\n");
        goto close_ifd;
    }

    shmring_reset(chan->tx, CHAN_SIZE);
    shmring_reset(chan->rx, CHAN_SIZE);

    shmring_init_mux(chan->tx);
    shmring_init_mux(chan->rx);

    /* Write number of cores to channel for guest proxy to receive */
    h_msg.msg_type = MSG_TYPE_HELLO;
    h_msg.n_cores = flexnic_info_pxy->cores_num;

    ret = channel_write(chan, &h_msg, sizeof(struct hello_msg));
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send number "
                "of cores to guest.\n");
        goto close_ifd;
    }
    notify_guest(ifd);
    
    pxy->vms[pxy->next_vm_id].ifd = ifd;
    pxy->vms[pxy->next_vm_id].nfd = nfd;
    pxy->vms[pxy->next_vm_id].id = pxy->next_vm_id;
    pxy->vms[pxy->next_vm_id].chan = chan;
    fprintf(stdout, "Connected VM=%d\n", pxy->next_vm_id);
    
    pxy->next_vm_id++;

    return 0;

close_ifd:
    close(ifd);
close_nfd:
    close(nfd);
close_cfd:
    close(cfd);

    return -1;
}

static int uxsocket_handle_notif()
{
    return 0;
}

static int uxsocket_handle_error()
{
    fprintf(stderr, "ivshmem_uxsocket_error: epoll_wait error.\n");
    return 0;
}


/*****************************************************************************/
/* Channel */

static int channel_poll(struct host_proxy *pxy)
{
    int i, n = 0;
    struct v_machine *vm;

    for(i = 0; i < pxy->next_vm_id; i++)
    {
        vm = &pxy->vms[i];
        if (vm != NULL)
        {
            if (channel_poll_vm(pxy, vm) > 0)
                n += 1;
        }
    }

    return n;
}

static int channel_poll_vm(struct host_proxy *pxy, 
        struct v_machine *vm)
{
    int ret, is_empty;
    void *msg;
    uint8_t msg_type;
    size_t msg_size;

    /* Move on if rx channel for this vm is empty */
    shmring_lock(vm->chan->rx);
    is_empty = shmring_is_empty(vm->chan->rx);
    shmring_unlock(vm->chan->rx);

    if (is_empty)
    {
        return 0;
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
            channel_handle_tasinforeq_msg(vm);
            break;
        case MSG_TYPE_CONTEXT_REQ:
            channel_handle_ctx_req(pxy, vm, msg);
            break;
        case MSG_TYPE_NEWAPP_REQ:
            channel_handle_newapp(pxy, vm, msg);
            break;
        case MSG_TYPE_POKE_TAS_CORE:
            channel_handle_poke_tas_core(vm, msg);
            break;
        case MSG_TYPE_POKE_TAS_KERNEL:
            channel_handle_poke_tas_kernel(vm, msg);
            break;
        default:
            fprintf(stderr, "ivshmem_uxsocket_handle_msg: unknown message.\n");
    }

    return 1;
}

/* Handles tasinfo request */
static int channel_handle_tasinforeq_msg(struct v_machine *vm)
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
    if (memcpy(msg->flexnic_info, flexnic_info_pxy, FLEXNIC_INFO_BYTES) == NULL)
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

    notify_guest(vm->ifd);

    return 0;

free_msg:
    free(msg);

    return -1;

}

static int channel_handle_ctx_req(struct host_proxy *pxy,
        struct v_machine *vm, struct context_req_msg *msg) 
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

  if (flextcp_proxy_context_create(ctx, res_msg.resp, 
        &res_msg.resp_size, vm->id, msg->app_id) == -1) 
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
  vctx->cfd = msg->actx_evfd;
  vctx->ctxreq_id = msg->ctxreq_id;
  vctx->app_id = msg->app_id;
  vctx->next = vm->ctxs;
  vctx->vm = vm;
  vm->ctxs = vctx;

  res_msg.msg_type = MSG_TYPE_CONTEXT_RES;
  res_msg.ctxreq_id = msg->ctxreq_id;
  res_msg.app_id = msg->app_id;

  ret = channel_write(vm->chan, &res_msg, sizeof(struct context_res_msg));
  if (ret < sizeof(struct context_res_msg))
  {
    fprintf(stderr, "ivshmem_handle_ctxreq: failed to send ctx res.\n");
    return -1;
  }

  /* add vctx to context request epoll */
  ev.events = EPOLLIN;
  ev.data.ptr = vctx;
  if (epoll_ctl(pxy->ctx_epfd, EPOLL_CTL_ADD, vctx->ctx->evfd, &ev) != 0) 
  {
    fprintf(stderr, "ivshmem_handle_ctxreq: epoll_ctl listen failed."); 
    return -1;
  }

  if (epoll_ctl(pxy->block_epfd, EPOLL_CTL_ADD, vctx->ctx->evfd, &ev) != 0) 
  {
    fprintf(stderr, "ivshmem_handle_ctxreq: epoll_ctl block_epfd failed."); 
    return -1;
  }

  notify_guest(vm->ifd);

  return 0;
}

static int channel_handle_newapp(struct host_proxy *pxy,
        struct v_machine *vm, struct newapp_req_msg *msg_req)
{
    int ret, appid;
    struct newapp_res_msg msg_res;
    appid = pxy->next_app_id[vm->id];
    pxy->next_app_id[vm->id]++;

    if (flextcp_proxy_newapp(vm->id, appid) < 0) 
    {
        fprintf(stderr, "ivshmem_handle_newapp: "
                "failed to register app with flextcp.\n");
        return -1;
    }

    msg_res.msg_type = MSG_TYPE_NEWAPP_RES;
    msg_res.cfd = msg_req->cfd;

    ret = channel_write(vm->chan, &msg_res, sizeof(msg_res));
    
    if (ret != sizeof(msg_res))
    {
        fprintf(stderr, "ivshmem_handle_newapp: "
                "failed to write response to channel.\n");
        return -1;
    }

    notify_guest(vm->ifd);

    return 0;
}

static void channel_handle_poke_tas_kernel(struct v_machine *vm, 
        struct poke_tas_kernel_msg *msg)
{
    uint64_t val = 1;
    int r = write(kernel_evfd_pxy, &val, sizeof(uint64_t));
    assert(r == sizeof(uint64_t));
}

static void channel_handle_poke_tas_core(struct v_machine *vm, 
        struct poke_tas_core_msg *msg)
{
    uint64_t val = 1;
    int r = write(flexnic_evfd_pxy[msg->core_id], &val, sizeof(uint64_t));
    assert(r == sizeof(uint64_t));
}

/*****************************************************************************/
/* App contexts */

static int app_ctxs_poll(struct host_proxy *pxy) 
{
    int i, n, ret, n_pokes = 0;
    struct vmcontext_req *vctx;
    struct _msg;
    struct epoll_event evs[2];
    struct poke_app_ctx_msg msg;

    n = epoll_wait(pxy->ctx_epfd, evs, 2, 0);

    if (n < 0)
    {
        fprintf(stderr, "ivshmem_ctxs_poll: epoll_wait failed\n");
        return -1;
    }

    for (i = 0; i < n; i++) 
    {
        if (evs[i].events & EPOLLIN) 
        {
            vctx = evs[i].data.ptr;
            ivshmem_drain_evfd(vctx->ctx->evfd);

            msg.msg_type = MSG_TYPE_POKE_APP_CTX;
            msg.ctxreq_id = vctx->ctxreq_id;
            ret = channel_write(vctx->vm->chan, &msg, sizeof(struct poke_app_ctx_msg));

            if (ret != sizeof(struct poke_app_ctx_msg))
            {
                fprintf(stderr, "ivshmem_ctxs_poll: failed to write poke msg.\n");
                return -1;
            }       
            notify_guest(vctx->vm->ifd);
            n_pokes++;
        }
    }

    return n_pokes;
}

/*****************************************************************************/
/* Others */

static int uxsocket_send_int(int fd, int64_t i)
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

static int uxsocket_sendfd(int uxfd, int fd, int64_t i)
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
