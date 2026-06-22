#ifndef FS_P2P_COMMON_NET_H
#define FS_P2P_COMMON_NET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

int net_ignore_sigpipe(void);
int net_connect(const char *host, const char *port);
int net_listen(const char *port, int backlog);
ssize_t net_send_all(int fd, const void *buf, size_t len);
ssize_t net_recv_all(int fd, void *buf, size_t len);
int net_send_msg(int fd, const void *payload, uint32_t len);
int net_recv_msg(int fd, void *buf, size_t capacity, uint32_t *len_out);
void net_close(int fd);

int net_normalize_ip_literal(const char *src, char *dst, size_t dst_size);
int net_get_local_ip(const char *remote_host, const char *remote_port, char *local_ip, size_t max_len);

uint64_t host_to_net64(uint64_t value);
uint64_t net_to_host64(uint64_t value);

#endif
