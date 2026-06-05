#include "server/query_handler.h"

#include "common/net.h"
#include "common/protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

typedef struct {
    p2p_msg_header_t header;
    union {
        register_req_t register_req;
        find_req_t find_req;
    } payload;
} client_req_frame_t;

typedef struct {
    p2p_msg_header_t header;
    union {
        register_resp_t register_resp;
        find_resp_t find_resp;
        error_msg_t error;
    } payload;
} server_resp_frame_t;

typedef struct {
    int client_fd;
    registry_t *registry;
    char client_ip[P2P_MAX_IP_LEN];
} worker_args_t;

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

static void copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0u) {
        return;
    }
    if (src == NULL) {
        src = "";
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

static void encode_header(p2p_msg_header_t *header,
                          p2p_opcode_t opcode,
                          uint32_t payload_len)
{
    memset(header, 0, sizeof(*header));
    header->opcode = htons((uint16_t)opcode);
    header->version = htons((uint16_t)P2P_PROTOCOL_VERSION);
    header->payload_len = htonl(payload_len);
}

static int decode_header(const p2p_msg_header_t *header,
                         uint16_t *opcode_out,
                         uint32_t *payload_len_out)
{
    uint16_t version;

    if (header == NULL || opcode_out == NULL || payload_len_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    version = ntohs(header->version);
    if (version != (uint16_t)P2P_PROTOCOL_VERSION) {
        errno = EPROTO;
        return -1;
    }

    *opcode_out = ntohs(header->opcode);
    *payload_len_out = ntohl(header->payload_len);
    return 0;
}

static void decode_file_meta(file_meta_t *dst, const file_meta_t *src)
{
    *dst = *src;
    dst->name[P2P_MAX_FILENAME - 1u] = '\0';
    dst->hash = net_to_host64(src->hash);
    dst->size_bytes = net_to_host64(src->size_bytes);
    dst->owner_ip[P2P_MAX_IP_LEN - 1u] = '\0';
    dst->owner_port = ntohs(src->owner_port);
}

static void encode_file_meta(file_meta_t *dst, const file_meta_t *src)
{
    memset(dst, 0, sizeof(*dst));
    copy_cstr(dst->name, sizeof(dst->name), src->name);
    dst->hash = host_to_net64(src->hash);
    dst->size_bytes = host_to_net64(src->size_bytes);
    copy_cstr(dst->owner_ip, sizeof(dst->owner_ip), src->owner_ip);
    dst->owner_port = htons(src->owner_port);
}

static void encode_peer(peer_entry_t *dst, const peer_entry_t *src)
{
    memset(dst, 0, sizeof(*dst));
    copy_cstr(dst->ip, sizeof(dst->ip), src->ip);
    dst->data_port = htons(src->data_port);
    dst->last_seen_epoch = host_to_net64(src->last_seen_epoch);
}

static int send_error(int fd, p2p_error_code_t code, const char *message)
{
    server_resp_frame_t response;
    uint32_t frame_len;

    memset(&response, 0, sizeof(response));
    encode_header(&response.header, P2P_MSG_ERROR, (uint32_t)sizeof(response.payload.error));
    response.payload.error.code = htonl((uint32_t)code);
    copy_cstr(response.payload.error.message,
              sizeof(response.payload.error.message),
              message);

    frame_len = (uint32_t)(sizeof(response.header) + sizeof(response.payload.error));
    return net_send_msg(fd, &response, frame_len);
}

static int send_register_response(int fd,
                                  registry_t *registry,
                                  const char *client_ip,
                                  uint16_t data_port)
{
    server_resp_frame_t response;
    peer_entry_t recent[REGISTRY_MAX_PEERS];
    size_t recent_count;
    size_t i;
    uint32_t frame_len;

    memset(&response, 0, sizeof(response));
    encode_header(&response.header,
                  P2P_MSG_REGISTER_RESP,
                  (uint32_t)sizeof(response.payload.register_resp));

    recent_count = registry_recent_peers(registry, recent, REGISTRY_MAX_PEERS);
    for (i = 0u; i < recent_count &&
         response.payload.register_resp.neighbor_count < P2P_MAX_NEIGHBORS; ++i) {
        if (strcmp(recent[i].ip, client_ip) == 0 &&
            recent[i].data_port == data_port) {
            continue;
        }

        encode_peer(&response.payload.register_resp.neighbors[
                        response.payload.register_resp.neighbor_count],
                    &recent[i]);
        response.payload.register_resp.neighbor_count++;
    }

    response.payload.register_resp.neighbor_count =
        htonl(response.payload.register_resp.neighbor_count);
    frame_len = (uint32_t)(sizeof(response.header) +
                           sizeof(response.payload.register_resp));
    return net_send_msg(fd, &response, frame_len);
}

static int handle_register(int fd,
                           registry_t *registry,
                           const char *client_ip,
                           const register_req_t *request)
{
    file_meta_t files[P2P_MAX_FILES_PER_PEER];
    uint32_t file_count;
    uint16_t data_port;
    size_t i;

    file_count = ntohl(request->file_count);
    if (file_count > P2P_MAX_FILES_PER_PEER) {
        return send_error(fd,
                          P2P_ERROR_BAD_REQUEST,
                          "REGISTER file_count exceeds protocol limit");
    }

    data_port = ntohs(request->data_port);
    for (i = 0u; i < file_count; ++i) {
        decode_file_meta(&files[i], &request->files[i]);
    }

    if (registry_register_peer(registry,
                               client_ip,
                               data_port,
                               files,
                               (size_t)file_count) != 0) {
        if (errno == ENOSPC) {
            return send_error(fd, P2P_ERROR_REGISTRY_FULL, "registry is full");
        }
        return send_error(fd, P2P_ERROR_INTERNAL, "failed to register peer");
    }

    return send_register_response(fd, registry, client_ip, data_port);
}

static int handle_find(int fd, registry_t *registry, const find_req_t *request)
{
    server_resp_frame_t response;
    file_meta_t results[P2P_MAX_RESULTS];
    char term[P2P_MAX_TERM];
    size_t result_count;
    size_t i;
    uint32_t frame_len;

    memset(term, 0, sizeof(term));
    memcpy(term, request->term, sizeof(term));
    term[P2P_MAX_TERM - 1u] = '\0';

    memset(results, 0, sizeof(results));
    result_count = registry_find_by_name(registry, term, results, P2P_MAX_RESULTS);

    memset(&response, 0, sizeof(response));
    encode_header(&response.header,
                  P2P_MSG_FIND_RESP,
                  (uint32_t)sizeof(response.payload.find_resp));
    response.payload.find_resp.result_count = htonl((uint32_t)result_count);
    for (i = 0u; i < result_count; ++i) {
        encode_file_meta(&response.payload.find_resp.results[i], &results[i]);
    }

    frame_len = (uint32_t)(sizeof(response.header) + sizeof(response.payload.find_resp));
    return net_send_msg(fd, &response, frame_len);
}

int query_handle_client(int client_fd, const char *client_ip, registry_t *registry)
{
    client_req_frame_t request;
    uint32_t frame_len = 0u;
    uint32_t payload_len = 0u;
    uint16_t opcode = 0u;
    int rc;

    if (client_fd < 0 || client_ip == NULL || registry == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    rc = net_recv_msg(client_fd, &request, sizeof(request), &frame_len);
    if (rc == 0) {
        return 0;
    }
    if (rc < 0) {
        return -1;
    }

    if (frame_len < sizeof(request.header)) {
        return send_error(client_fd,
                          P2P_ERROR_BAD_REQUEST,
                          "frame is smaller than protocol header");
    }

    if (decode_header(&request.header, &opcode, &payload_len) != 0) {
        return send_error(client_fd,
                          P2P_ERROR_UNSUPPORTED_VERSION,
                          "unsupported protocol version");
    }

    if (payload_len != frame_len - (uint32_t)sizeof(request.header)) {
        return send_error(client_fd,
                          P2P_ERROR_BAD_REQUEST,
                          "payload length does not match frame length");
    }

    switch ((p2p_opcode_t)opcode) {
    case P2P_MSG_REGISTER_REQ:
        if (payload_len != sizeof(request.payload.register_req)) {
            return send_error(client_fd,
                              P2P_ERROR_BAD_REQUEST,
                              "REGISTER payload length is invalid");
        }
        return handle_register(client_fd,
                               registry,
                               client_ip,
                               &request.payload.register_req);
    case P2P_MSG_FIND_REQ:
        if (payload_len != sizeof(request.payload.find_req)) {
            return send_error(client_fd,
                              P2P_ERROR_BAD_REQUEST,
                              "FIND payload length is invalid");
        }
        return handle_find(client_fd, registry, &request.payload.find_req);
    default:
        return send_error(client_fd,
                          P2P_ERROR_UNKNOWN_OPCODE,
                          "unknown server opcode");
    }
}

static void *client_worker(void *arg)
{
    worker_args_t *worker = (worker_args_t *)arg;

    if (worker != NULL) {
        if (query_handle_client(worker->client_fd,
                                worker->client_ip,
                                worker->registry) != 0) {
            perror("query_handle_client");
        }
        net_close(worker->client_fd);
        free(worker);
    }

    return NULL;
}

static void sockaddr_to_ip(const struct sockaddr *addr, socklen_t addr_len, char *ip, size_t ip_len)
{
    int rc;

    copy_cstr(ip, ip_len, "0.0.0.0");
    if (addr == NULL || ip == NULL || ip_len == 0u) {
        return;
    }

    rc = getnameinfo(addr,
                     addr_len,
                     ip,
                     (socklen_t)ip_len,
                     NULL,
                     0,
                     NI_NUMERICHOST);
    if (rc != 0) {
        copy_cstr(ip, ip_len, "0.0.0.0");
    }
}

int query_server_run(const char *port, registry_t *registry)
{
    int listen_fd;

    listen_fd = net_listen(port, 16);
    if (listen_fd < 0) {
        perror("net_listen");
        return -1;
    }

    printf("p2p-server listening on port %s\n", port);
    for (;;) {
        struct sockaddr_storage addr;
        socklen_t addr_len = (socklen_t)sizeof(addr);
        worker_args_t *worker;
        pthread_t thread;
        int client_fd;

        client_fd = accept(listen_fd, (struct sockaddr *)&addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            net_close(listen_fd);
            return -1;
        }

        worker = (worker_args_t *)calloc(1u, sizeof(*worker));
        if (worker == NULL) {
            perror("calloc");
            net_close(client_fd);
            continue;
        }

        worker->client_fd = client_fd;
        worker->registry = registry;
        sockaddr_to_ip((const struct sockaddr *)&addr,
                       addr_len,
                       worker->client_ip,
                       sizeof(worker->client_ip));

        if (pthread_create(&thread, NULL, client_worker, worker) != 0) {
            perror("pthread_create");
            (void)query_handle_client(worker->client_fd,
                                      worker->client_ip,
                                      worker->registry);
            net_close(worker->client_fd);
            free(worker);
            continue;
        }

        (void)pthread_detach(thread);
    }

    net_close(listen_fd);
    return 0;
}
