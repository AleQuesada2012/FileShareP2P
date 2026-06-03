#include "common/hash.h"
#include "common/net.h"
#include "transfer/receiver.h"
#include "transfer/sender.h"

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    p2p_msg_header_t header;
    transfer_data_header_t data_header;
    unsigned char data[4];
} transfer_data_frame_t;

static int is_little_endian(void)
{
    const uint16_t value = 1u;
    return *((const unsigned char *)&value) == 1u;
}

static uint64_t host_to_net64(uint64_t value)
{
    if (!is_little_endian()) {
        return value;
    }

    return ((uint64_t)htonl((uint32_t)(value & UINT64_C(0xffffffff))) << 32) |
           (uint64_t)htonl((uint32_t)(value >> 32));
}

static void encode_header(p2p_msg_header_t *header, p2p_opcode_t opcode, uint32_t payload_len)
{
    memset(header, 0, sizeof(*header));
    header->opcode = htons((uint16_t)opcode);
    header->version = htons((uint16_t)P2P_PROTOCOL_VERSION);
    header->payload_len = htonl(payload_len);
}

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

static void read_file_or_die(const char *path, char *buffer, size_t len)
{
    FILE *file = fopen(path, "rb");

    assert(file != NULL);
    assert(fread(buffer, 1u, len, file) == len);
    assert(fclose(file) == 0);
}

static void send_manual_data_frame(int fd,
                                   uint64_t hash,
                                   uint64_t size,
                                   uint64_t byte_start,
                                   const char *data,
                                   uint32_t data_len)
{
    transfer_data_frame_t frame;
    uint32_t payload_len;
    uint32_t frame_len;

    assert(data_len <= sizeof(frame.data));
    memset(&frame, 0, sizeof(frame));
    payload_len = (uint32_t)(sizeof(frame.data_header) + data_len);
    frame_len = (uint32_t)(sizeof(frame.header) + payload_len);
    encode_header(&frame.header, P2P_MSG_TRANSFER_DATA, payload_len);
    frame.data_header.hash = host_to_net64(hash);
    frame.data_header.size = host_to_net64(size);
    frame.data_header.byte_start = host_to_net64(byte_start);
    frame.data_header.data_len = htonl(data_len);
    memcpy(frame.data, data, data_len);
    assert(net_send_msg(fd, &frame, frame_len) == 0);
}

int main(void)
{
    const char payload[] = "receiver payload";
    char source_path[] = "/tmp/fs-p2p-receiver-source-XXXXXX";
    char output_path[] = "/tmp/fs-p2p-receiver-output-XXXXXX";
    char corrupt_output_path[] = "/tmp/fs-p2p-receiver-corrupt-XXXXXX";
    char downloaded[sizeof(payload)];
    transfer_req_t request;
    uint64_t hash;
    uint64_t size;
    int source_fd;
    int output_fd;
    int corrupt_output_fd;
    int sockets[2];
    int corrupt_sockets[2];

    /* Create a source file that the real sender can validate and read. */
    source_fd = mkstemp(source_path);
    assert(source_fd >= 0);
    write_all_or_die(source_fd, payload, strlen(payload));
    assert(close(source_fd) == 0);
    assert(hash_file_with_size(source_path, &hash, &size) == 0);

    /* Create the destination file descriptor used by the receiver. */
    output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(ftruncate(output_fd, (off_t)size) == 0);

    /* Request the complete source file through a local socketpair. */
    memset(&request, 0, sizeof(request));
    request.hash = hash;
    request.size = size;
    request.byte_start = 0u;
    request.byte_end = size - 1u;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(transfer_send_range(sockets[0], source_path, &request) == 0);
    assert(transfer_receive_segment(sockets[1], output_fd, hash, size, 0u, size - 1u) == 0);

    /* Verify the receiver assembled exactly the bytes emitted by the sender. */
    assert(close(output_fd) == 0);
    memset(downloaded, 0, sizeof(downloaded));
    read_file_or_die(output_path, downloaded, strlen(payload));
    assert(memcmp(downloaded, payload, strlen(payload)) == 0);

    /* Reject duplicated data frames that would otherwise leave a range gap. */
    corrupt_output_fd = mkstemp(corrupt_output_path);
    assert(corrupt_output_fd >= 0);
    assert(ftruncate(corrupt_output_fd, 2) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, corrupt_sockets) == 0);
    send_manual_data_frame(corrupt_sockets[0], hash, 2u, 0u, "a", 1u);
    send_manual_data_frame(corrupt_sockets[0], hash, 2u, 0u, "b", 1u);
    assert(transfer_receive_segment(corrupt_sockets[1], corrupt_output_fd, hash, 2u, 0u, 1u) != 0);
    assert(close(corrupt_output_fd) == 0);

    /* Clean up all temporary files and local sockets. */
    net_close(sockets[0]);
    net_close(sockets[1]);
    net_close(corrupt_sockets[0]);
    net_close(corrupt_sockets[1]);
    assert(unlink(source_path) == 0);
    assert(unlink(output_path) == 0);
    assert(unlink(corrupt_output_path) == 0);

    puts("test_transfer_receiver: ok");
    return 0;
}
