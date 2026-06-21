#include "client/server_api.h"

#include "common/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    p2p_msg_header_t header;
    register_req_t payload;
} register_req_frame_t;

typedef struct {
    p2p_msg_header_t header;
    find_req_t payload;
} find_req_frame_t;

typedef struct {
    p2p_msg_header_t header;
    union {
        register_resp_t register_resp;
        find_resp_t find_resp;
        error_msg_t error;
    } payload;
} server_resp_frame_t;



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

static void encode_header(p2p_msg_header_t *header, p2p_opcode_t opcode, uint32_t payload_len)
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

static void encode_file_meta(file_meta_t *dst, const file_meta_t *src)
{
    memset(dst, 0, sizeof(*dst));
    copy_cstr(dst->name, sizeof(dst->name), src->name);
    dst->hash = host_to_net64(src->hash);
    dst->size_bytes = host_to_net64(src->size_bytes);
    copy_cstr(dst->owner_ip, sizeof(dst->owner_ip), src->owner_ip);
    dst->owner_port = htons(src->owner_port);
}

static void decode_file_meta(file_meta_t *dst, const file_meta_t *src)
{
    char owner_ip[P2P_MAX_IP_LEN];

    *dst = *src;
    dst->name[P2P_MAX_FILENAME - 1u] = '\0';
    dst->hash = net_to_host64(src->hash);
    dst->size_bytes = net_to_host64(src->size_bytes);
    dst->owner_ip[P2P_MAX_IP_LEN - 1u] = '\0';
    copy_cstr(owner_ip, sizeof(owner_ip), dst->owner_ip);
    if (net_normalize_ip_literal(owner_ip, dst->owner_ip, sizeof(dst->owner_ip)) != 0) {
        copy_cstr(dst->owner_ip, sizeof(dst->owner_ip), owner_ip);
    }
    dst->owner_port = ntohs(src->owner_port);
}

static void decode_peer(peer_entry_t *dst, const peer_entry_t *src)
{
    char ip[P2P_MAX_IP_LEN];

    *dst = *src;
    dst->ip[P2P_MAX_IP_LEN - 1u] = '\0';
    copy_cstr(ip, sizeof(ip), dst->ip);
    if (net_normalize_ip_literal(ip, dst->ip, sizeof(dst->ip)) != 0) {
        copy_cstr(dst->ip, sizeof(dst->ip), ip);
    }
    dst->data_port = ntohs(src->data_port);
    dst->last_seen_epoch = net_to_host64(src->last_seen_epoch);
}

static int recv_server_frame(int fd, server_resp_frame_t *frame, uint32_t *payload_len_out)
{
    uint32_t frame_len = 0u;
    uint16_t opcode;
    uint32_t payload_len;
    int rc;

    memset(frame, 0, sizeof(*frame));
    rc = net_recv_msg(fd, frame, sizeof(*frame), &frame_len);
    if (rc <= 0) {
        errno = rc == 0 ? ECONNRESET : errno;
        return -1;
    }
    if (frame_len < sizeof(frame->header)) {
        errno = EPROTO;
        return -1;
    }
    if (decode_header(&frame->header, &opcode, &payload_len) != 0) {
        return -1;
    }
    if (payload_len != frame_len - (uint32_t)sizeof(frame->header)) {
        errno = EPROTO;
        return -1;
    }

    if (opcode == (uint16_t)P2P_MSG_ERROR) {
        error_msg_t error = frame->payload.error;
        error.message[P2P_MAX_ERROR_MESSAGE - 1u] = '\0';
        fprintf(stderr, "server error %u: %s\n", ntohl(error.code), error.message);
        errno = EPROTO;
        return -1;
    }

    *payload_len_out = payload_len;
    return (int)opcode;
}

int server_register_files(const char *server_ip,
                          const char *server_port,
                          uint16_t data_port,
                          const scan_result_t *scan,
                          register_resp_t *response_out)
{
    register_req_frame_t request;
    server_resp_frame_t response;
    uint32_t payload_len = 0u;
    uint32_t neighbor_count;
    size_t file_count;
    size_t i;
    int fd;
    int opcode;

    if (server_ip == NULL || server_port == NULL || scan == NULL || response_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    encode_header(&request.header, P2P_MSG_REGISTER_REQ, (uint32_t)sizeof(request.payload));
    request.payload.data_port = htons(data_port);
    file_count = scan->count > P2P_MAX_FILES_PER_PEER ? P2P_MAX_FILES_PER_PEER : scan->count;
    request.payload.file_count = htonl((uint32_t)file_count);
    for (i = 0u; i < file_count; ++i) {
        encode_file_meta(&request.payload.files[i], &scan->files[i]);
    }

    fd = net_connect(server_ip, server_port);
    if (fd < 0) {
        return -1;
    }

    if (net_send_msg(fd, &request, (uint32_t)sizeof(request)) != 0) {
        net_close(fd);
        return -1;
    }

    opcode = recv_server_frame(fd, &response, &payload_len);
    net_close(fd);
    if (opcode != (int)P2P_MSG_REGISTER_RESP || payload_len != sizeof(response.payload.register_resp)) {
        errno = EPROTO;
        return -1;
    }

    memset(response_out, 0, sizeof(*response_out));
    neighbor_count = ntohl(response.payload.register_resp.neighbor_count);
    if (neighbor_count > P2P_MAX_NEIGHBORS) {
        errno = EPROTO;
        return -1;
    }
    response_out->neighbor_count = neighbor_count;
    for (i = 0u; i < neighbor_count; ++i) {
        decode_peer(&response_out->neighbors[i], &response.payload.register_resp.neighbors[i]);
    }

    return 0;
}

int server_find_files(const char *server_ip,
                      const char *server_port,
                      const char *term,
                      search_results_t *results_out)
{
    find_req_frame_t request;
    server_resp_frame_t response;
    uint32_t payload_len = 0u;
    uint32_t result_count;
    size_t i;
    int fd;
    int opcode;

    if (server_ip == NULL || server_port == NULL || term == NULL || results_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&request, 0, sizeof(request));
    encode_header(&request.header, P2P_MSG_FIND_REQ, (uint32_t)sizeof(request.payload));
    copy_cstr(request.payload.term, sizeof(request.payload.term), term);

    fd = net_connect(server_ip, server_port);
    if (fd < 0) {
        return -1;
    }

    if (net_send_msg(fd, &request, (uint32_t)sizeof(request)) != 0) {
        net_close(fd);
        return -1;
    }

    opcode = recv_server_frame(fd, &response, &payload_len);
    net_close(fd);
    if (opcode != (int)P2P_MSG_FIND_RESP || payload_len != sizeof(response.payload.find_resp)) {
        errno = EPROTO;
        return -1;
    }

    memset(results_out, 0, sizeof(*results_out));
    result_count = ntohl(response.payload.find_resp.result_count);
    if (result_count > P2P_MAX_RESULTS) {
        errno = EPROTO;
        return -1;
    }
    results_out->count = result_count;
    for (i = 0u; i < result_count; ++i) {
        decode_file_meta(&results_out->items[i], &response.payload.find_resp.results[i]);
    }

    return 0;
}
