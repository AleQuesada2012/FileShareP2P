#include "common/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int net_ignore_sigpipe(void)
{
    return signal(SIGPIPE, SIG_IGN) == SIG_ERR ? -1 : 0;
}

int net_connect(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    int fd = -1;
    int rc;

    if (host == NULL || port == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(host, port, &hints, &result);
    if (rc != 0) {
        errno = EHOSTUNREACH;
        return -1;
    }

    for (it = result; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }

        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

int net_listen(const char *port, int backlog)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    int fd = -1;
    int yes = 1;
    int rc;

    if (port == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    rc = getaddrinfo(NULL, port, &hints, &result);
    if (rc != 0) {
        errno = EADDRNOTAVAIL;
        return -1;
    }

    for (it = result; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }

        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        if (bind(fd, it->ai_addr, it->ai_addrlen) == 0 &&
            listen(fd, backlog > 0 ? backlog : SOMAXCONN) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

ssize_t net_send_all(int fd, const void *buf, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)buf;
    size_t sent_total = 0u;

    if (buf == NULL && len > 0u) {
        errno = EINVAL;
        return -1;
    }

    while (sent_total < len) {
        ssize_t sent = send(fd, bytes + sent_total, len - sent_total, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            errno = EPIPE;
            return -1;
        }
        sent_total += (size_t)sent;
    }

    return (ssize_t)sent_total;
}

ssize_t net_recv_all(int fd, void *buf, size_t len)
{
    unsigned char *bytes = (unsigned char *)buf;
    size_t recv_total = 0u;

    if (buf == NULL && len > 0u) {
        errno = EINVAL;
        return -1;
    }

    while (recv_total < len) {
        ssize_t received = recv(fd, bytes + recv_total, len - recv_total, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (received == 0) {
            break;
        }
        recv_total += (size_t)received;
    }

    return (ssize_t)recv_total;
}

int net_send_msg(int fd, const void *payload, uint32_t len)
{
    uint32_t wire_len = htonl(len);

    if (payload == NULL && len > 0u) {
        errno = EINVAL;
        return -1;
    }

    if (net_send_all(fd, &wire_len, sizeof(wire_len)) != (ssize_t)sizeof(wire_len)) {
        return -1;
    }

    if (len == 0u) {
        return 0;
    }

    return net_send_all(fd, payload, len) == (ssize_t)len ? 0 : -1;
}

int net_recv_msg(int fd, void *buf, size_t capacity, uint32_t *len_out)
{
    uint32_t wire_len;
    uint32_t len;
    ssize_t received;

    if (len_out == NULL || (buf == NULL && capacity > 0u)) {
        errno = EINVAL;
        return -1;
    }

    received = net_recv_all(fd, &wire_len, sizeof(wire_len));
    if (received == 0) {
        return 0;
    }
    if (received != (ssize_t)sizeof(wire_len)) {
        errno = ECONNRESET;
        return -1;
    }

    len = ntohl(wire_len);
    if ((size_t)len > capacity) {
        errno = EMSGSIZE;
        return -1;
    }

    received = net_recv_all(fd, buf, len);
    if (received != (ssize_t)len) {
        errno = ECONNRESET;
        return -1;
    }

    *len_out = len;
    return 1;
}

int net_get_local_ip(const char *remote_host, const char *remote_port, char *local_ip, size_t max_len)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    int fd = -1;
    int rc;

    if (remote_host == NULL || remote_port == NULL || local_ip == NULL || max_len == 0) {
        errno = EINVAL;
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    rc = getaddrinfo(remote_host, remote_port, &hints, &result);
    if (rc != 0) {
        return -1;
    }

    for (it = result; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }

        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            struct sockaddr_storage local_addr;
            socklen_t addr_len = sizeof(local_addr);
            if (getsockname(fd, (struct sockaddr *)&local_addr, &addr_len) == 0) {
                if (local_addr.ss_family == AF_INET) {
                    struct sockaddr_in *s = (struct sockaddr_in *)&local_addr;
                    inet_ntop(AF_INET, &s->sin_addr, local_ip, max_len);
                } else if (local_addr.ss_family == AF_INET6) {
                    struct sockaddr_in6 *s = (struct sockaddr_in6 *)&local_addr;
                    inet_ntop(AF_INET6, &s->sin6_addr, local_ip, max_len);
                }
                close(fd);
                freeaddrinfo(result);
                return 0;
            }
        }

        close(fd);
    }

    freeaddrinfo(result);
    return -1;
}

void net_close(int fd)
{
    if (fd >= 0) {
        while (close(fd) != 0 && errno == EINTR) {
        }
    }
}

static int is_little_endian(void)
{
    const uint16_t value = 1u;
    return *((const unsigned char *)&value) == 1u;
}

uint64_t host_to_net64(uint64_t value)
{
    if (!is_little_endian()) {
        return value;
    }

    return ((uint64_t)htonl((uint32_t)(value & UINT64_C(0xffffffff))) << 32) |
           (uint64_t)htonl((uint32_t)(value >> 32));
}

uint64_t net_to_host64(uint64_t value)
{
    return host_to_net64(value);
}
