#include "common/hash.h"
#include "common/net.h"
#include "common/protocol.h"
#include "transfer/sender.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    p2p_msg_header_t header;
    transfer_data_header_t data_header;
    unsigned char data[32];
} transfer_data_frame_t;



static void write_all_or_die(int fd, const void *data, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t written_total = 0u;

    while (written_total < len) {
        ssize_t written = write(fd, bytes + written_total, len - written_total);
        assert(written > 0);
        written_total += (size_t)written;
    }
}

int main(void)
{
    const char payload[] = "abcdef";
    char path[] = "/tmp/fs-p2p-transfer-XXXXXX";
    transfer_req_t request;
    transfer_data_frame_t frame;
    uint64_t hash;
    uint64_t size;
    uint32_t frame_len = 0u;
    int fd;
    int sockets[2];

    /* Create a temporary source file that the sender can validate and read. */
    fd = mkstemp(path);
    assert(fd >= 0);
    write_all_or_die(fd, payload, strlen(payload));
    assert(close(fd) == 0);
    assert(hash_file_with_size(path, &hash, &size) == 0);

    /* Request the byte range for "bcd" using the file identity tuple. */
    memset(&request, 0, sizeof(request));
    request.hash = hash;
    request.size = size;
    request.byte_start = 1u;
    request.byte_end = 3u;

    /* Use socketpair so the sender can be tested without a real remote peer. */
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(transfer_send_range(sockets[0], path, &request) == 0);
    assert(net_recv_msg(sockets[1], &frame, sizeof(frame), &frame_len) == 1);

    /* Verify the sender emitted one TRANSFER_DATA frame with the right metadata. */
    assert(frame_len == sizeof(frame.header) + sizeof(frame.data_header) + 3u);
    assert(ntohs(frame.header.opcode) == (uint16_t)P2P_MSG_TRANSFER_DATA);
    assert(ntohs(frame.header.version) == (uint16_t)P2P_PROTOCOL_VERSION);
    assert(ntohl(frame.header.payload_len) == sizeof(frame.data_header) + 3u);
    assert(net_to_host64(frame.data_header.hash) == hash);
    assert(net_to_host64(frame.data_header.size) == size);
    assert(net_to_host64(frame.data_header.byte_start) == 1u);
    assert(ntohl(frame.data_header.data_len) == 3u);

    /* The payload bytes must match exactly the requested source range. */
    assert(memcmp(frame.data, "bcd", 3u) == 0);

    net_close(sockets[0]);
    net_close(sockets[1]);

    /* Clean up the temporary source fixture. */
    assert(unlink(path) == 0);

    puts("test_transfer_sender: ok");
    return 0;
}
