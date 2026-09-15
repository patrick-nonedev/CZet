/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Minimal AF_UNIX SOCK_STREAM IPC.
 *
 * Header-only `static` functions, libc only.  Return 0 on success,
 * -1 on error with errno preserved.
 *
 *   ipc_server_open   bind (after unlink), listen, accept one client.
 *   ipc_server_close  close both fds, unlink path.
 *   ipc_client_open   connect to path.
 *   ipc_client_close  close fd.
 *   ipc_send_all / ipc_recv_all  exactly len bytes.
 */
#ifndef CZET_UTILS_IPC_H
#define CZET_UTILS_IPC_H

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define IPC_LISTEN_BACKLOG 8

static int ipc_server_open(const char *socket_path, int *server_fd,
                           int *client_fd)
{
    struct sockaddr_un addr;
    socklen_t addrlen;
    size_t path_len;

    if (!socket_path || !server_fd || !client_fd)
    {
        errno = EINVAL;
        return -1;
    }
    path_len = strlen(socket_path);
    if (path_len == 0 || path_len >= sizeof(addr.sun_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    *server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (*server_fd < 0)
        return -1;
    unlink(socket_path);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, socket_path, path_len + 1);
    addrlen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1);
    if (bind(*server_fd, (struct sockaddr *)&addr, addrlen) != 0)
    {
        int e = errno;
        close(*server_fd);
        errno = e;
        return -1;
    }
    if (listen(*server_fd, IPC_LISTEN_BACKLOG) != 0)
    {
        int e = errno;
        close(*server_fd);
        unlink(socket_path);
        errno = e;
        return -1;
    }
    *client_fd = accept(*server_fd, NULL, NULL);
    if (*client_fd < 0)
    {
        int e = errno;
        close(*server_fd);
        unlink(socket_path);
        errno = e;
        return -1;
    }
    return 0;
}

static int ipc_server_close(const char *socket_path, int *server_fd,
                            int *client_fd)
{
    if (client_fd && *client_fd >= 0)
    {
        close(*client_fd);
        *client_fd = -1;
    }
    if (server_fd && *server_fd >= 0)
    {
        close(*server_fd);
        *server_fd = -1;
    }
    if (socket_path)
        unlink(socket_path);
    return 0;
}

static int ipc_client_open(const char *socket_path, int *fd)
{
    struct sockaddr_un addr;
    socklen_t addrlen;
    size_t path_len;

    if (!socket_path || !fd)
    {
        errno = EINVAL;
        return -1;
    }
    path_len = strlen(socket_path);
    if (path_len == 0 || path_len >= sizeof(addr.sun_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    *fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (*fd < 0)
        return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, socket_path, path_len + 1);
    addrlen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1);
    if (connect(*fd, (struct sockaddr *)&addr, addrlen) != 0)
    {
        int e = errno;
        close(*fd);
        errno = e;
        return -1;
    }
    return 0;
}

static int ipc_client_close(int *fd)
{
    if (fd && *fd >= 0)
    {
        close(*fd);
        *fd = -1;
    }
    return 0;
}

/* Send exactly len bytes (retry EINTR).  0 ok, -1 error. */
static int ipc_send_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    while (len > 0)
    {
        ssize_t w = send(fd, p, len, 0);
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (w == 0)
        {
            errno = EPIPE;
            return -1;
        }
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

/* Receive exactly len bytes (retry EINTR).  0 ok, -1 error/EOF. */
static int ipc_recv_all(int fd, void *buf, size_t len)
{
    unsigned char *p = (unsigned char *)buf;
    while (len > 0)
    {
        ssize_t r = recv(fd, p, len, 0);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
        {
            errno = ECONNRESET;
            return -1;
        }
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

#endif /* CZET_UTILS_IPC_H */
