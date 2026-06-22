#include "common/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
    NET_MAX_HOST_LITERAL = 256,
    NET_CONNECT_ATTEMPTS = 3
};

static int should_retry_connect(int error)
{
    return error == EINTR ||
           error == ECONNREFUSED ||
           error == EHOSTUNREACH ||
           error == ENETUNREACH ||
           error == ETIMEDOUT;
}

static void sleep_before_retry(void)
{
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 100000000L;
    while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
    }
}

static int copy_string(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (dst == NULL || dst_size == 0u || src == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (dst == src) {
        return 0;
    }

    len = strlen(src);
    if (len >= dst_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(dst, src, len + 1u);
    return 0;
}

int net_ignore_sigpipe(void)
{
    return signal(SIGPIPE, SIG_IGN) == SIG_ERR ? -1 : 0;
}

int net_normalize_ip_literal(const char *src, char *dst, size_t dst_size)
{
    static const char mapped_prefix[] = "::ffff:";
    struct in_addr mapped_v4;
    const char *mapped_value;

    if (src == NULL || dst == NULL || dst_size == 0u) {
        errno = EINVAL;
        return -1;
    }

    mapped_value = src + sizeof(mapped_prefix) - 1u;
    if (strncmp(src, mapped_prefix, sizeof(mapped_prefix) - 1u) == 0 &&
        inet_pton(AF_INET, mapped_value, &mapped_v4) == 1) {
        return inet_ntop(AF_INET, &mapped_v4, dst, (socklen_t)dst_size) == NULL ? -1 : 0;
    }

    return copy_string(dst, dst_size, src);
}

static int parse_tcp_port(const char *port, uint16_t *port_out)
{
    char *end = NULL;
    unsigned long value;

    if (port == NULL || port_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    value = strtoul(port, &end, 10);
    if (errno != 0 || end == port || *end != '\0' || value > 65535ul) {
        errno = EINVAL;
        return -1;
    }

    *port_out = (uint16_t)value;
    return 0;
}

static int connect_sockaddr_with_retry(int family,
                                       int socktype,
                                       int protocol,
                                       const struct sockaddr *addr,
                                       socklen_t addr_len)
{
    int saved_errno = EHOSTUNREACH;
    int attempt;

    for (attempt = 0; attempt < NET_CONNECT_ATTEMPTS; ++attempt) {
        int fd = socket(family, socktype, protocol);
        if (fd < 0) {
            saved_errno = errno;
            continue;
        }

        if (connect(fd, addr, addr_len) == 0) {
            return fd;
        }

        saved_errno = errno;
        close(fd);
        if (!should_retry_connect(saved_errno) || attempt + 1 == NET_CONNECT_ATTEMPTS) {
            break;
        }
        sleep_before_retry();
    }

    errno = saved_errno;
    return -1;
}

static int connect_numeric_host(const char *host, const char *port, int *attempted_out)
{
    uint16_t port_num;
    struct in_addr ipv4;
    struct in6_addr ipv6;

    if (attempted_out != NULL) {
        *attempted_out = 0;
    }

    if (parse_tcp_port(port, &port_num) != 0) {
        if (attempted_out != NULL) {
            *attempted_out = 1;
        }
        return -1;
    }

    if (inet_pton(AF_INET, host, &ipv4) == 1) {
        struct sockaddr_in addr;

        if (attempted_out != NULL) {
            *attempted_out = 1;
        }
        memset(&addr, 0, sizeof(addr));
#if defined(__APPLE__) && defined(__MACH__)
        addr.sin_len = (unsigned char)sizeof(addr);
#endif
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_num);
        addr.sin_addr = ipv4;
        return connect_sockaddr_with_retry(AF_INET,
                                           SOCK_STREAM,
                                           0,
                                           (const struct sockaddr *)&addr,
                                           (socklen_t)sizeof(addr));
    }

    if (inet_pton(AF_INET6, host, &ipv6) == 1) {
        struct sockaddr_in6 addr;

        if (attempted_out != NULL) {
            *attempted_out = 1;
        }
        memset(&addr, 0, sizeof(addr));
#if defined(__APPLE__) && defined(__MACH__)
        addr.sin6_len = (unsigned char)sizeof(addr);
#endif
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port_num);
        addr.sin6_addr = ipv6;
        return connect_sockaddr_with_retry(AF_INET6,
                                           SOCK_STREAM,
                                           0,
                                           (const struct sockaddr *)&addr,
                                           (socklen_t)sizeof(addr));
    }

    errno = EAFNOSUPPORT;
    return -1;
}

int net_connect(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    char normalized_host[NET_MAX_HOST_LITERAL];
    int fd = -1;
    int numeric_attempted = 0;
    int saved_errno = EHOSTUNREACH;
    int rc;

    if (host == NULL || port == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (net_normalize_ip_literal(host, normalized_host, sizeof(normalized_host)) != 0) {
        return -1;
    }

    fd = connect_numeric_host(normalized_host, port, &numeric_attempted);
    if (fd >= 0) {
        return fd;
    }
    saved_errno = errno;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(normalized_host, port, &hints, &result);
    if (rc != 0) {
        errno = EHOSTUNREACH;
        return -1;
    }

    for (it = result; it != NULL; it = it->ai_next) {
        fd = connect_sockaddr_with_retry(it->ai_family,
                                         it->ai_socktype,
                                         it->ai_protocol,
                                         it->ai_addr,
                                         it->ai_addrlen);
        if (fd >= 0) {
            break;
        }
        saved_errno = errno;
    }

    freeaddrinfo(result);
    if (fd < 0) {
        errno = saved_errno;
    }
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
    char normalized_remote[NET_MAX_HOST_LITERAL];
    int fd = -1;
    int rc;

    if (remote_host == NULL || remote_port == NULL || local_ip == NULL || max_len == 0) {
        errno = EINVAL;
        return -1;
    }

    if (net_normalize_ip_literal(remote_host, normalized_remote, sizeof(normalized_remote)) != 0) {
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    rc = getaddrinfo(normalized_remote, remote_port, &hints, &result);
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
                    if (inet_ntop(AF_INET, &s->sin_addr, local_ip, (socklen_t)max_len) == NULL) {
                        close(fd);
                        freeaddrinfo(result);
                        return -1;
                    }
                } else if (local_addr.ss_family == AF_INET6) {
                    struct sockaddr_in6 *s = (struct sockaddr_in6 *)&local_addr;
                    if (IN6_IS_ADDR_V4MAPPED(&s->sin6_addr)) {
                        struct in_addr mapped_v4;
                        memcpy(&mapped_v4, &s->sin6_addr.s6_addr[12], sizeof(mapped_v4));
                        if (inet_ntop(AF_INET, &mapped_v4, local_ip, (socklen_t)max_len) == NULL) {
                            close(fd);
                            freeaddrinfo(result);
                            return -1;
                        }
                    } else if (inet_ntop(AF_INET6, &s->sin6_addr, local_ip, (socklen_t)max_len) == NULL) {
                        close(fd);
                        freeaddrinfo(result);
                        return -1;
                    }
                } else {
                    close(fd);
                    freeaddrinfo(result);
                    errno = EAFNOSUPPORT;
                    return -1;
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
