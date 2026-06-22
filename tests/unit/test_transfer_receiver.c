#include "common/hash.h"
#include "common/net.h"
#include "transfer/receiver.h"
#include "transfer/sender.h"

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    p2p_msg_header_t header;
    transfer_data_header_t data_header;
    unsigned char data[4];
} transfer_data_frame_t;

typedef struct {
    p2p_msg_header_t header;
    transfer_req_t payload;
} transfer_req_frame_t;

typedef struct {
    int listen_fd;
    char source_path[PATH_MAX];
} transfer_server_context_t;

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

static int open_loopback_listener(uint16_t *port_out)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int fd;
    int yes = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    assert(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    assert(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(fd, 1) == 0);
    assert(getsockname(fd, (struct sockaddr *)&addr, &addr_len) == 0);
    *port_out = ntohs(addr.sin_port);
    return fd;
}

static void *transfer_server_main(void *arg)
{
    transfer_server_context_t *ctx = (transfer_server_context_t *)arg;
    transfer_req_frame_t frame;
    transfer_req_t request;
    uint32_t frame_len = 0u;
    int client_fd;

    client_fd = accept(ctx->listen_fd, NULL, NULL);
    assert(client_fd >= 0);
    assert(net_recv_msg(client_fd, &frame, sizeof(frame), &frame_len) == 1);
    assert(frame_len == sizeof(frame));
    assert(ntohs(frame.header.opcode) == (uint16_t)P2P_MSG_TRANSFER_REQ);
    assert(ntohl(frame.header.payload_len) == sizeof(frame.payload));

    request.hash = net_to_host64(frame.payload.hash);
    request.size = net_to_host64(frame.payload.size);
    request.byte_start = net_to_host64(frame.payload.byte_start);
    request.byte_end = net_to_host64(frame.payload.byte_end);
    assert(transfer_send_range(client_fd, ctx->source_path, &request) == 0);

    net_close(client_fd);
    net_close(ctx->listen_fd);
    return NULL;
}

int main(void)
{
    const char payload[] = "receiver payload";
    char source_path[] = "/tmp/fs-p2p-receiver-source-XXXXXX";
    char output_path[] = "/tmp/fs-p2p-receiver-output-XXXXXX";
    char corrupt_output_path[] = "/tmp/fs-p2p-receiver-corrupt-XXXXXX";
    char request_source_dir[] = "/tmp/fs-p2p-receiver-source-dir-XXXXXX";
    char request_dest_dir[] = "/tmp/fs-p2p-receiver-dest-dir-XXXXXX";
    char request_source_path[PATH_MAX];
    char request_download_path[PATH_MAX];
    char downloaded[sizeof(payload)];
    transfer_req_t request;
    transfer_server_context_t server_ctx;
    peer_entry_t peer;
    uint64_t hash;
    uint64_t size;
    uint64_t request_hash;
    uint64_t request_size;
    int source_fd;
    int output_fd;
    int corrupt_output_fd;
    int sockets[2];
    int corrupt_sockets[2];
    pthread_t server_thread;
    uint16_t listen_port;

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

    /* A full transfer_request should preserve only the original basename in the requester folder. */
    assert(mkdtemp(request_source_dir) != NULL);
    assert(mkdtemp(request_dest_dir) != NULL);
    assert(snprintf(request_source_path,
                    sizeof(request_source_path),
                    "%s/%s",
                    request_source_dir,
                    "original-apple.pdf") < (int)sizeof(request_source_path));
    source_fd = open(request_source_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    assert(source_fd >= 0);
    write_all_or_die(source_fd, payload, strlen(payload));
    assert(close(source_fd) == 0);
    assert(hash_file_with_size(request_source_path, &request_hash, &request_size) == 0);

    memset(&server_ctx, 0, sizeof(server_ctx));
    assert(snprintf(server_ctx.source_path,
                    sizeof(server_ctx.source_path),
                    "%s",
                    request_source_path) < (int)sizeof(server_ctx.source_path));
    server_ctx.listen_fd = open_loopback_listener(&listen_port);
    assert(pthread_create(&server_thread, NULL, transfer_server_main, &server_ctx) == 0);

    memset(&peer, 0, sizeof(peer));
    assert(snprintf(peer.ip, sizeof(peer.ip), "%s", "127.0.0.1") < (int)sizeof(peer.ip));
    peer.data_port = listen_port;
    assert(transfer_request(request_hash,
                            request_size,
                            &peer,
                            1u,
                            "remote/subdir/original-apple.pdf",
                            request_dest_dir) == 0);
    assert(pthread_join(server_thread, NULL) == 0);
    assert(snprintf(request_download_path,
                    sizeof(request_download_path),
                    "%s/%s",
                    request_dest_dir,
                    "original-apple.pdf") < (int)sizeof(request_download_path));
    memset(downloaded, 0, sizeof(downloaded));
    read_file_or_die(request_download_path, downloaded, strlen(payload));
    assert(memcmp(downloaded, payload, strlen(payload)) == 0);

    /* Clean up all temporary files and local sockets. */
    net_close(sockets[0]);
    net_close(sockets[1]);
    net_close(corrupt_sockets[0]);
    net_close(corrupt_sockets[1]);
    assert(unlink(source_path) == 0);
    assert(unlink(output_path) == 0);
    assert(unlink(corrupt_output_path) == 0);
    assert(unlink(request_source_path) == 0);
    assert(unlink(request_download_path) == 0);
    assert(rmdir(request_source_dir) == 0);
    assert(rmdir(request_dest_dir) == 0);

    puts("test_transfer_receiver: ok");
    return 0;
}
