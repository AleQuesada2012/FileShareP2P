#include "common/net.h"
#include "common/protocol.h"
#include "server/query_handler.h"
#include "server/registry.h"

#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

typedef struct {
    p2p_msg_header_t header;
    register_req_t payload;
} register_frame_t;

typedef struct {
    p2p_msg_header_t header;
    find_req_t payload;
} find_frame_t;

typedef struct {
    p2p_msg_header_t header;
    union {
        register_resp_t register_resp;
        find_resp_t find_resp;
        error_msg_t error;
    } payload;
} response_frame_t;

typedef struct {
    int fd;
    const char *ip;
    registry_t *registry;
    int rc;
} handler_thread_t;

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

static uint64_t net_to_host64(uint64_t value)
{
    return host_to_net64(value);
}

static void encode_header(p2p_msg_header_t *header, p2p_opcode_t opcode, uint32_t payload_len)
{
    memset(header, 0, sizeof(*header));
    header->opcode = htons((uint16_t)opcode);
    header->version = htons((uint16_t)P2P_PROTOCOL_VERSION);
    header->payload_len = htonl(payload_len);
}

static void encode_file(file_meta_t *file,
                        const char *name,
                        uint64_t hash,
                        uint64_t size,
                        const char *owner_ip,
                        uint16_t owner_port)
{
    memset(file, 0, sizeof(*file));
    (void)snprintf(file->name, sizeof(file->name), "%s", name);
    file->hash = host_to_net64(hash);
    file->size_bytes = host_to_net64(size);
    (void)snprintf(file->owner_ip, sizeof(file->owner_ip), "%s", owner_ip);
    file->owner_port = htons(owner_port);
}

static void read_response(int fd, response_frame_t *response, uint32_t *frame_len)
{
    memset(response, 0, sizeof(*response));
    assert(net_recv_msg(fd, response, sizeof(*response), frame_len) == 1);
    assert(*frame_len >= sizeof(response->header));
    assert(ntohs(response->header.version) == (uint16_t)P2P_PROTOCOL_VERSION);
    assert(ntohl(response->header.payload_len) ==
           *frame_len - (uint32_t)sizeof(response->header));
}

static void *run_handler(void *arg)
{
    handler_thread_t *handler = (handler_thread_t *)arg;

    handler->rc = query_handle_client(handler->fd, handler->ip, handler->registry);
    return NULL;
}

static void start_handler(pthread_t *thread,
                          handler_thread_t *handler,
                          int fd,
                          const char *ip,
                          registry_t *registry)
{
    handler->fd = fd;
    handler->ip = ip;
    handler->registry = registry;
    handler->rc = -99;
    assert(pthread_create(thread, NULL, run_handler, handler) == 0);
}

static void join_handler(pthread_t thread, handler_thread_t *handler)
{
    assert(pthread_join(thread, NULL) == 0);
    assert(handler->rc == 0);
}

static void send_register_request(int fd,
                                  const char *name,
                                  uint64_t hash,
                                  uint64_t size,
                                  uint16_t data_port)
{
    register_frame_t request;

    memset(&request, 0, sizeof(request));
    encode_header(&request.header, P2P_MSG_REGISTER_REQ, (uint32_t)sizeof(request.payload));
    request.payload.data_port = htons(data_port);
    request.payload.file_count = htonl(1u);
    encode_file(&request.payload.files[0], name, hash, size, "ignored", 1u);
    assert(net_send_msg(fd, &request, (uint32_t)sizeof(request)) == 0);
}

static void send_find_request(int fd, const char *term)
{
    find_frame_t request;

    memset(&request, 0, sizeof(request));
    encode_header(&request.header, P2P_MSG_FIND_REQ, (uint32_t)sizeof(request.payload));
    (void)snprintf(request.payload.term, sizeof(request.payload.term), "%s", term);
    assert(net_send_msg(fd, &request, (uint32_t)sizeof(request)) == 0);
}

static void test_register_find_and_neighbors(registry_t *registry)
{
    response_frame_t response;
    file_meta_t registry_results[4];
    handler_thread_t handler;
    pthread_t thread;
    uint32_t frame_len;
    int fds[2];

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    start_handler(&thread, &handler, fds[1], "10.0.0.1", registry);
    send_register_request(fds[0], "alpha-video.mp4", UINT64_C(0xabc), 1234u, 7001u);
    read_response(fds[0], &response, &frame_len);
    join_handler(thread, &handler);
    assert(ntohs(response.header.opcode) == (uint16_t)P2P_MSG_REGISTER_RESP);
    assert(frame_len == sizeof(response.header) + sizeof(response.payload.register_resp));
    assert(ntohl(response.payload.register_resp.neighbor_count) == 0u);
    net_close(fds[0]);
    net_close(fds[1]);

    assert(registry_find_by_name(registry, "alpha", registry_results, 4u) == 1u);
    assert(strcmp(registry_results[0].owner_ip, "10.0.0.1") == 0);
    assert(registry_results[0].owner_port == 7001u);
    assert(registry_results[0].hash == UINT64_C(0xabc));
    assert(registry_results[0].size_bytes == 1234u);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    start_handler(&thread, &handler, fds[1], "10.0.0.2", registry);
    send_register_request(fds[0], "beta.iso", UINT64_C(0xdef), 5678u, 7002u);
    read_response(fds[0], &response, &frame_len);
    join_handler(thread, &handler);
    assert(ntohs(response.header.opcode) == (uint16_t)P2P_MSG_REGISTER_RESP);
    assert(ntohl(response.payload.register_resp.neighbor_count) == 1u);
    assert(strcmp(response.payload.register_resp.neighbors[0].ip, "10.0.0.1") == 0);
    assert(ntohs(response.payload.register_resp.neighbors[0].data_port) == 7001u);
    net_close(fds[0]);
    net_close(fds[1]);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    start_handler(&thread, &handler, fds[1], "10.0.0.9", registry);
    send_find_request(fds[0], "alpha");
    read_response(fds[0], &response, &frame_len);
    join_handler(thread, &handler);
    assert(ntohs(response.header.opcode) == (uint16_t)P2P_MSG_FIND_RESP);
    assert(frame_len == sizeof(response.header) + sizeof(response.payload.find_resp));
    assert(ntohl(response.payload.find_resp.result_count) == 1u);
    assert(strcmp(response.payload.find_resp.results[0].name, "alpha-video.mp4") == 0);
    assert(net_to_host64(response.payload.find_resp.results[0].hash) == UINT64_C(0xabc));
    assert(net_to_host64(response.payload.find_resp.results[0].size_bytes) == 1234u);
    assert(strcmp(response.payload.find_resp.results[0].owner_ip, "10.0.0.1") == 0);
    assert(ntohs(response.payload.find_resp.results[0].owner_port) == 7001u);
    net_close(fds[0]);
    net_close(fds[1]);
}

static void test_malformed_frame_returns_error(registry_t *registry)
{
    response_frame_t response;
    find_frame_t request;
    handler_thread_t handler;
    pthread_t thread;
    uint32_t frame_len;
    int fds[2];

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    start_handler(&thread, &handler, fds[1], "10.0.0.3", registry);
    memset(&request, 0, sizeof(request));
    encode_header(&request.header,
                  P2P_MSG_FIND_REQ,
                  (uint32_t)(sizeof(request.payload) + 1u));
    assert(net_send_msg(fds[0], &request, (uint32_t)sizeof(request)) == 0);
    read_response(fds[0], &response, &frame_len);
    join_handler(thread, &handler);
    assert(ntohs(response.header.opcode) == (uint16_t)P2P_MSG_ERROR);
    assert(ntohl(response.payload.error.code) == (uint32_t)P2P_ERROR_BAD_REQUEST);
    net_close(fds[0]);
    net_close(fds[1]);
}

static void test_early_disconnect_is_safe(registry_t *registry)
{
    handler_thread_t handler;
    pthread_t thread;
    int fds[2];

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    start_handler(&thread, &handler, fds[1], "10.0.0.4", registry);
    net_close(fds[0]);
    join_handler(thread, &handler);
    net_close(fds[1]);
}

int main(void)
{
    registry_t registry;

    assert(registry_init(&registry) == 0);
    test_register_find_and_neighbors(&registry);
    test_malformed_frame_returns_error(&registry);
    test_early_disconnect_is_safe(&registry);
    registry_destroy(&registry);

    puts("test_server_query_handler: ok");
    return 0;
}
