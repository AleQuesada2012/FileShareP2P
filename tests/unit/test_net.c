#include "common/net.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int fds[2];
    const char payload[] = "ping";
    char buffer[32];
    uint32_t len = 0u;

    /*
     * Use socketpair to test the message helpers without depending on an
     * external server or a specific TCP port.
     */
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    /* net_send_msg/net_recv_msg should preserve both frame length and bytes. */
    assert(net_send_msg(fds[0], payload, (uint32_t)sizeof(payload)) == 0);
    assert(net_recv_msg(fds[1], buffer, sizeof(buffer), &len) == 1);
    assert(len == sizeof(payload));
    assert(memcmp(buffer, payload, sizeof(payload)) == 0);

    net_close(fds[0]);
    net_close(fds[1]);

    /* IPv4-mapped IPv6 literals should be dialed and advertised as plain IPv4 on mixed macOS/Linux LANs. */
    assert(net_normalize_ip_literal("::ffff:192.168.1.26", buffer, sizeof(buffer)) == 0);
    assert(strcmp(buffer, "192.168.1.26") == 0);

    /* Already-normal IPv4 literals should pass through unchanged. */
    assert(net_normalize_ip_literal("127.0.0.1", buffer, sizeof(buffer)) == 0);
    assert(strcmp(buffer, "127.0.0.1") == 0);

    puts("test_net: ok");
    return 0;
}
