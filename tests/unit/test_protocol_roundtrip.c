#include "common/net.h"
#include "common/protocol.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

typedef struct {
    p2p_msg_header_t header;
    find_req_t payload;
} find_frame_t;

int main(void)
{
    int fds[2];
    find_frame_t sent;
    find_frame_t received;
    uint32_t received_len = 0u;

    memset(&sent, 0, sizeof(sent));
    memset(&received, 0, sizeof(received));

    /*
     * Build the same frame shape used on the wire: protocol header first,
     * followed by the opcode-specific FIND payload.
     */
    sent.header.opcode = htons((uint16_t)P2P_MSG_FIND_REQ);
    sent.header.version = htons((uint16_t)P2P_PROTOCOL_VERSION);
    sent.header.payload_len = htonl((uint32_t)sizeof(sent.payload));
    (void)snprintf(sent.payload.term, sizeof(sent.payload.term), "%s", "video.mp4");

    /* Round-trip the full protocol frame through the shared net helpers. */
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    assert(net_send_msg(fds[0], &sent, (uint32_t)sizeof(sent)) == 0);
    assert(net_recv_msg(fds[1], &received, sizeof(received), &received_len) == 1);

    /* Decode network byte order fields and verify the payload survived intact. */
    assert(received_len == sizeof(received));
    assert(ntohs(received.header.opcode) == (uint16_t)P2P_MSG_FIND_REQ);
    assert(ntohs(received.header.version) == (uint16_t)P2P_PROTOCOL_VERSION);
    assert(ntohl(received.header.payload_len) == sizeof(received.payload));
    assert(strcmp(received.payload.term, "video.mp4") == 0);

    net_close(fds[0]);
    net_close(fds[1]);

    puts("test_protocol_roundtrip: ok");
    return 0;
}
