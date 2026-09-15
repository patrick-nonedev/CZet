/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Minimal TCP networking (IPv4/IPv6 via getaddrinfo).
 *
 * Header-only `static` functions.  Return fd (>= 0) or 0 on success,
 * -1 on error with errno preserved.  No extra link flags.
 *
 *   tcp_listen(host, port, backlog)  listen; host NULL = any iface.
 *                                    port "0" = ephemeral (see tcp_port).
 *   tcp_accept(srv)                  accept one client.
 *   tcp_connect(host, port)          connect as client.
 *   tcp_send_all / tcp_recv_all      exactly len bytes.
 *   tcp_port(fd)                     bound local port, or -1.
 *   tcp_close(fd)                    close.
 */
#ifndef CZET_UTILS_SERVER_H
#define CZET_UTILS_SERVER_H

/* getaddrinfo/getnameinfo need this under strict -std=czet. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int tcp_listen(const char *host, const char *port, int backlog)
{
    struct addrinfo hints, *res = NULL, *rp;
    int fd = -1, one = 1, rc;

    if (!port)
    {
        errno = EINVAL;
        return -1;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0)
    {
        errno = EIO;
        return -1;
    }
    for (rp = res; rp; rp = rp->ai_next)
    {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0
            && listen(fd, backlog > 0 ? backlog : 8) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0 && errno == 0)
        errno = EIO;
    return fd;
}

static int tcp_accept(int srv_fd)
{
    int fd;
    do
    {
        fd = accept(srv_fd, NULL, NULL);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

static int tcp_connect(const char *host, const char *port)
{
    struct addrinfo hints, *res = NULL, *rp;
    int fd = -1, rc;

    if (!host || !port)
    {
        errno = EINVAL;
        return -1;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0)
    {
        errno = EIO;
        return -1;
    }
    for (rp = res; rp; rp = rp->ai_next)
    {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0 && errno == 0)
        errno = ECONNREFUSED;
    return fd;
}

static int tcp_send_all(int fd, const void *buf, size_t len)
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

static int tcp_recv_all(int fd, void *buf, size_t len)
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

/* Local port of a bound socket (for listen with port "0"). */
static int tcp_port(int fd)
{
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    char host[64], port[16];

    if (getsockname(fd, (struct sockaddr *)&ss, &sl) != 0)
        return -1;
    if (getnameinfo((struct sockaddr *)&ss, sl, host, sizeof(host), port,
                    sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV) != 0)
        return -1;
    {
        int p = 0;
        if (sscanf(port, "%d", &p) != 1)
            return -1;
        return p;
    }
}

static int tcp_close(int fd)
{
    if (fd >= 0)
        close(fd);
    return 0;
}

#endif /* CZET_UTILS_SERVER_H */
