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
/* ID of next virtual machine to connect */
static int next_id = 0;
/* List of VMs */
static struct v_machine vms[MAX_VMS];

static int ivshmem_uxsocket_init();
static int ivshmem_poll_uxsocket();
static int ivshmem_uxsocket_accept();
static int ivshmem_uxsocket_notif();
static int ivshmem_uxsocket_error();
static int ivshmem_uxsocket_newmsg();
static int ivshmem_uxsocket_send_int(int fd, int64_t i);
static int ivshmem_uxsocket_sendfd(int uxfd, int fd, int64_t i);

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
    if ((epfd = epoll_create1(0)) == -1) 
    {
        fprintf(stderr, "ivshmem_init: epoll_create1 failed.\n"); 
        goto close_uxfd;
    }

    /* Add unix socket to interest list */
    ev.events = EPOLLIN;
    ev.data.u32 = EP_LISTEN;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, uxfd, &ev) != 0) {
        fprintf(stderr, "ivshmem_init: epoll_ctl listen failed.\n"); 
        goto close_epfd;
    }

    return 0;

close_epfd:
        close(epfd);
close_uxfd:
        close(uxfd);

    return -1;
}

int ivshmem_poll() 
{
    if (ivshmem_poll_uxsocket() != 0)
    {
        fprintf(stderr, "ivshmem_poll: failed to poll uxsocket.\n");
        return -1;
    }

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

static int ivshmem_poll_uxsocket()
{
    int i, n;
    struct epoll_event evs[32];
    struct epoll_event ev;

    if ((n = epoll_wait(epfd, evs, 32, 0)) < 0)
    {
        fprintf(stderr, "poll_ivshmem_uxsocket: epoll_wait failed.\n");
        return -1;
    }

    /* Handle each event from epoll */
    for (i = 0; i < n; i++)
    {
        ev = evs[i];

        if (ev.data.u32 == EP_LISTEN)
        {
            ivshmem_uxsocket_accept();
        }
        else if (ev.data.u32 == EP_NOTIFY)
        {
            // TODO: Figure out whether we need this
            ivshmem_uxsocket_notif();
        }
        else if (evs[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
        {
            ivshmem_uxsocket_error();
        }
        else if (evs[i].events & EPOLLIN)
        {
            ivshmem_uxsocket_newmsg();
        }
    }

    return 0;
}

/* Accepts connection when a qemu vm starts and uses the
   uxsocket to set up the shared memory region */
static int ivshmem_uxsocket_accept()
{   
    int ret, n_cores;
    int cfd, ifd, nfd;
    void *tx_addr, *rx_addr;
    struct epoll_event ev;
    struct channel *chan;

    int64_t version = IVSHMEM_PROTOCOL_VERSION;
    int64_t hostid = HOST_PEERID;

    /* Return error if max number of VMs has been reached */
    if (next_id > MAX_VMS)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: max vms reached.\n");
        return -1;
    }

    /* Accept connection from qemu ivshmem */
    if ((cfd = accept(uxfd, NULL, NULL)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: accept failed.\n");
        return -1;
    }
    printf("cfd accepted.\n");

    /* Send protocol version as required by qemu ivshmem */
    ret = ivshmem_uxsocket_send_int(cfd, version);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send "
                "protocol version.\n");
        goto close_cfd;
    }

    /* Send vm id to the guest proxy */
    ret = ivshmem_uxsocket_send_int(cfd, next_id);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send vm id.\n");
        goto close_cfd;
    }
    printf("sent vm id.\n");

    /* Send shared memory fd to the guest proxy */
    ret = ivshmem_uxsocket_sendfd(cfd, flexnic_shmfd, -1);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send shm fd.\n");
        goto close_cfd;
    }
    printf("sent shm fd.\n");

    /* Create and send notify fd to the guest proxy */
    if ((nfd = eventfd(0, EFD_NONBLOCK)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_acccept: failed to create"
                "notify fd.\n");
        goto close_cfd;
    }
    printf("created notify fd.\n");

    /* Ivshmem protocol requires to send host id
       with the notify fd */
    ret = ivshmem_uxsocket_sendfd(cfd, nfd, hostid);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send "
                "notify fd.\n");
        goto close_ifd;
        
    }
    printf("sent notify fd.\n");

    /* Create and send interrupt fd to the guest proxy */
    if ((ifd = eventfd(0, EFD_NONBLOCK)) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to create "
                "interrupt fd.\n");
        goto close_nfd;
    }
    printf("created interrupt fd.\n");

    ret = ivshmem_uxsocket_sendfd(cfd, ifd, next_id);
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send "
                "interrupt fd.\n");
        goto close_ifd;
    }
    printf("sent interrupt fd.\n");

    /* Add connection fd to the epoll interest list */
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to add "
                "cfd to epoll.\n");
        goto close_ifd;
    }
    printf("added cfd to epoll.\n");

    /* Add notify fd to the epoll interest list */
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, nfd, &ev) < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to add "
                "nfd to epoll.\n");
        goto close_ifd;
    }
    printf("added nfd to epoll.\n");

    /* Init channel in shared memory */
    tx_addr = flexnic_mem + CHAN_OFFSET + (next_id * CHAN_SIZE * 2)
            + CHAN_SIZE;
    rx_addr = flexnic_mem + CHAN_OFFSET + (next_id * CHAN_SIZE * 2);
    chan = channel_init(tx_addr, rx_addr, CHAN_SIZE);
    if (chan == NULL)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to init chan.\n");
        goto close_ifd;
    }
    printf("created chan.\n");

    /* Write number of cores to channel for guest proxy to receive */
    n_cores = flexnic_info->cores_num;
    ret = channel_write(chan, &n_cores, sizeof(n_cores));
    if (ret < 0)
    {
        fprintf(stderr, "ivshmem_uxsocket_accept: failed to send number "
                "of cores to guest.\n");
    }
    printf("wrote to chan.\n");

    vms[next_id].ifd = ifd;
    vms[next_id].nfd = nfd;
    vms[next_id].id = next_id;
    vms[next_id].chan = chan;
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

static int ivshmem_uxsocket_notif()
{
    return 0;
}

static int ivshmem_uxsocket_error()
{
    // fprintf(stderr, "ivshmem_uxsocket_error: epoll_wait error.\n");
    return 0;
}

static int ivshmem_uxsocket_newmsg()
{
    // TODO: Handle the different types of messages
    //   - TAS_INFO
    //   - CONTEXT_REQ
    //   - ACK
    return 0;
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