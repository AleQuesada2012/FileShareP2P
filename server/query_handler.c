#include "server/query_handler.h"

#include "common/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

typedef struct {
    p2p_msg_header_t header;
    register_req_t payload;
} register_req_frame_t;

typedef struct {
    p2p_msg_header_t header;
    register_resp_t payload;
} register_resp_frame_t;

typedef struct {
    p2p_msg_header_t header;
    find_req_t payload;
} find_req_frame_t;

typedef struct {
    p2p_msg_header_t header;
    find_resp_t payload;
} find_resp_frame_t;

typedef struct {
    p2p_msg_header_t header;
    error_msg_t payload;
} error_frame_t;

typedef union {
    p2p_msg_header_t header;
    register_req_frame_t register_req;
    find_req_frame_t find_req;
} request_frame_t;

typedef struct {
    int fd;
    char peer_ip[P2P_MAX_IP_LEN];
    registry_t *registry;
} client_ctx_t;

static void server_log(const char *fmt, ...)
{
    va_list args;

    printf("[server] ");
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
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

static void encode_header(p2p_msg_header_t *header, p2p_opcode_t opcode, uint32_t payload_len)
{
    memset(header, 0, sizeof(*header));
    header->opcode = htons((uint16_t)opcode);
    header->version = htons((uint16_t)P2P_PROTOCOL_VERSION);
    header->payload_len = htonl(payload_len);
}

static int decode_header(const p2p_msg_header_t *header,
                         uint16_t *opcode_out,
                         uint32_t *payload_len_out,
                         p2p_error_code_t *error_out)
{
    if (header == NULL || opcode_out == NULL || payload_len_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (ntohs(header->version) != (uint16_t)P2P_PROTOCOL_VERSION) {
        if (error_out != NULL) {
            *error_out = P2P_ERROR_UNSUPPORTED_VERSION;
        }
        errno = EPROTO;
        return -1;
    }

    *opcode_out = ntohs(header->opcode);
    *payload_len_out = ntohl(header->payload_len);
    return 0;
}

static void decode_file_meta(file_meta_t *dst, const file_meta_t *src)
{
    memset(dst, 0, sizeof(*dst));
    copy_cstr(dst->name, sizeof(dst->name), src->name);
    dst->hash = net_to_host64(src->hash);
    dst->size_bytes = net_to_host64(src->size_bytes);
    copy_cstr(dst->owner_ip, sizeof(dst->owner_ip), src->owner_ip);
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

static int parse_uint64_token(const char *text, const char **next_out, uint64_t *value_out)
{
    char *end = NULL;
    unsigned long long value;

    if (text == NULL || value_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text) {
        return -1;
    }

    *value_out = (uint64_t)value;
    if (next_out != NULL) {
        *next_out = end;
    }
    return 0;
}

static const char *skip_identity_separator(const char *text)
{
    while (*text == ' ' || *text == '\t' || *text == ':' || *text == ',' || *text == ';') {
        ++text;
    }
    return text;
}

static int parse_identity_find_term(const char *term, uint64_t *size_out, uint64_t *hash_out)
{
    const char *cursor = term;
    uint64_t first;
    uint64_t second;

    if (term == NULL || size_out == NULL || hash_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (strncmp(cursor, "S=", 2u) == 0 || strncmp(cursor, "s=", 2u) == 0) {
        cursor += 2;
        if (parse_uint64_token(cursor, &cursor, &first) != 0) {
            return -1;
        }
        cursor = skip_identity_separator(cursor);
        if (strncmp(cursor, "H=", 2u) == 0 || strncmp(cursor, "h=", 2u) == 0) {
            cursor += 2;
        }
        if (parse_uint64_token(cursor, &cursor, &second) != 0) {
            return -1;
        }
        cursor = skip_identity_separator(cursor);
        if (*cursor != '\0') {
            return -1;
        }
        *size_out = first;
        *hash_out = second;
        return 0;
    }

    if (strncmp(cursor, "H=", 2u) == 0 || strncmp(cursor, "h=", 2u) == 0) {
        cursor += 2;
        if (parse_uint64_token(cursor, &cursor, &first) != 0) {
            return -1;
        }
        cursor = skip_identity_separator(cursor);
        if (strncmp(cursor, "S=", 2u) == 0 || strncmp(cursor, "s=", 2u) == 0) {
            cursor += 2;
        }
        if (parse_uint64_token(cursor, &cursor, &second) != 0) {
            return -1;
        }
        cursor = skip_identity_separator(cursor);
        if (*cursor != '\0') {
            return -1;
        }
        *hash_out = first;
        *size_out = second;
        return 0;
    }

    if (parse_uint64_token(cursor, &cursor, &first) != 0) {
        return -1;
    }
    cursor = skip_identity_separator(cursor);
    if (parse_uint64_token(cursor, &cursor, &second) != 0) {
        return -1;
    }
    cursor = skip_identity_separator(cursor);
    if (*cursor != '\0') {
        return -1;
    }

    *size_out = first;
    *hash_out = second;
    return 0;
}

static int send_error(int fd, p2p_error_code_t code, const char *message)
{
    error_frame_t frame;

    memset(&frame, 0, sizeof(frame));
    encode_header(&frame.header, P2P_MSG_ERROR, (uint32_t)sizeof(frame.payload));
    frame.payload.code = htonl((uint32_t)code);
    copy_cstr(frame.payload.message, sizeof(frame.payload.message), message);

    return net_send_msg(fd, &frame, (uint32_t)sizeof(frame));
}

static int handle_register(int fd,
                           registry_t *registry,
                           const char *peer_ip,
                           const register_req_t *payload)
{
    register_resp_frame_t response;
    peer_entry_t recent[P2P_MAX_NEIGHBORS];
    file_meta_t files[P2P_MAX_FILES_PER_PEER];
    uint32_t file_count;
    size_t recent_count;
    size_t i;
    uint16_t data_port;

    file_count = ntohl(payload->file_count);
    data_port = ntohs(payload->data_port);
    server_log("REGISTER request from %s:%u with %u file(s)",
               peer_ip,
               (unsigned)data_port,
               (unsigned)file_count);
    if (file_count > P2P_MAX_FILES_PER_PEER) {
        server_log("REGISTER rejected from %s:%u: too many files (%u)",
                   peer_ip,
                   (unsigned)data_port,
                   (unsigned)file_count);
        return send_error(fd, P2P_ERROR_BAD_REQUEST, "too many files in register request");
    }

    memset(files, 0, sizeof(files));
    for (i = 0u; i < file_count; ++i) {
        decode_file_meta(&files[i], &payload->files[i]);
    }

    if (registry_register_peer(registry, peer_ip, data_port, files, file_count) != 0) {
        if (errno == ENOSPC) {
            server_log("REGISTER failed for %s:%u: registry full",
                       peer_ip,
                       (unsigned)data_port);
            return send_error(fd, P2P_ERROR_REGISTRY_FULL, "server registry is full");
        }
        server_log("REGISTER failed for %s:%u: internal registry update error",
                   peer_ip,
                   (unsigned)data_port);
        return send_error(fd, P2P_ERROR_INTERNAL, "could not update registry");
    }

    recent_count = registry_recent_peers_except(registry, peer_ip, data_port, recent, P2P_MAX_NEIGHBORS);
    server_log("REGISTER ok for %s:%u: stored %u file(s), returned %zu neighbor(s)",
               peer_ip,
               (unsigned)data_port,
               (unsigned)file_count,
               recent_count);

    memset(&response, 0, sizeof(response));
    encode_header(&response.header, P2P_MSG_REGISTER_RESP, (uint32_t)sizeof(response.payload));
    response.payload.neighbor_count = htonl((uint32_t)recent_count);
    for (i = 0u; i < recent_count; ++i) {
        encode_peer(&response.payload.neighbors[i], &recent[i]);
    }

    return net_send_msg(fd, &response, (uint32_t)sizeof(response));
}

static int handle_find(int fd, registry_t *registry, const find_req_t *payload)
{
    find_resp_frame_t response;
    file_meta_t results[P2P_MAX_RESULTS];
    size_t result_count;
    size_t i;
    char term[P2P_MAX_TERM];
    uint64_t identity_size;
    uint64_t identity_hash;

    copy_cstr(term, sizeof(term), payload->term);
    if (term[0] == '\0') {
        return send_error(fd, P2P_ERROR_BAD_REQUEST, "find term is empty");
    }

    if (parse_identity_find_term(term, &identity_size, &identity_hash) == 0) {
        result_count = registry_find_by_identity(registry,
                                                 identity_hash,
                                                 identity_size,
                                                 results,
                                                 P2P_MAX_RESULTS);
        server_log("FIND identity request S=%llu H=%llu -> %zu result(s)",
                   (unsigned long long)identity_size,
                   (unsigned long long)identity_hash,
                   result_count);
    } else {
        result_count = registry_find_by_name(registry, term, results, P2P_MAX_RESULTS);
        server_log("FIND name request term=\"%s\" -> %zu result(s)",
                   term,
                   result_count);
    }

    memset(&response, 0, sizeof(response));
    encode_header(&response.header, P2P_MSG_FIND_RESP, (uint32_t)sizeof(response.payload));
    response.payload.result_count = htonl((uint32_t)result_count);
    for (i = 0u; i < result_count; ++i) {
        encode_file_meta(&response.payload.results[i], &results[i]);
    }

    return net_send_msg(fd, &response, (uint32_t)sizeof(response));
}

static void *client_thread_main(void *arg)
{
    client_ctx_t *ctx = (client_ctx_t *)arg;
    request_frame_t request;
    uint32_t frame_len = 0u;
    uint32_t payload_len = 0u;
    uint16_t opcode = 0u;
    p2p_error_code_t header_error = P2P_ERROR_BAD_REQUEST;
    int rc;

    memset(&request, 0, sizeof(request));
    rc = net_recv_msg(ctx->fd, &request, sizeof(request), &frame_len);
    if (rc <= 0) {
        if (rc == 0) {
            server_log("connection from %s closed before a request was received", ctx->peer_ip);
        } else {
            server_log("failed to receive request from %s: %s", ctx->peer_ip, strerror(errno));
        }
        goto done;
    }

    if (frame_len < sizeof(p2p_msg_header_t) ||
        decode_header(&request.header, &opcode, &payload_len, &header_error) != 0 ||
        payload_len != frame_len - (uint32_t)sizeof(p2p_msg_header_t)) {
        server_log("invalid request header from %s: frame_len=%u payload_len=%u",
                   ctx->peer_ip,
                   frame_len,
                   payload_len);
        (void)send_error(ctx->fd, header_error, "invalid message header");
        goto done;
    }

    server_log("processing opcode %u from %s", (unsigned)opcode, ctx->peer_ip);

    switch ((p2p_opcode_t)opcode) {
    case P2P_MSG_REGISTER_REQ:
        if (payload_len != sizeof(register_req_t)) {
            server_log("REGISTER rejected from %s: invalid payload length %u",
                       ctx->peer_ip,
                       payload_len);
            (void)send_error(ctx->fd, P2P_ERROR_BAD_REQUEST, "invalid register payload length");
            break;
        }
        (void)handle_register(ctx->fd, ctx->registry, ctx->peer_ip, &request.register_req.payload);
        break;
    case P2P_MSG_FIND_REQ:
        if (payload_len != sizeof(find_req_t)) {
            server_log("FIND rejected from %s: invalid payload length %u",
                       ctx->peer_ip,
                       payload_len);
            (void)send_error(ctx->fd, P2P_ERROR_BAD_REQUEST, "invalid find payload length");
            break;
        }
        (void)handle_find(ctx->fd, ctx->registry, &request.find_req.payload);
        break;
    default:
        server_log("unsupported opcode %u from %s", (unsigned)opcode, ctx->peer_ip);
        (void)send_error(ctx->fd, P2P_ERROR_UNKNOWN_OPCODE, "unsupported server opcode");
        break;
    }

 done:
    net_close(ctx->fd);
    free(ctx);
    return NULL;
}

static void socket_ip_string(const struct sockaddr *addr, socklen_t addr_len, char *dst, size_t dst_size)
{
    char raw[P2P_MAX_IP_LEN];
    int rc;

    copy_cstr(dst, dst_size, "unknown");
    copy_cstr(raw, sizeof(raw), "unknown");
    rc = getnameinfo(addr, addr_len, raw, (socklen_t)sizeof(raw), NULL, 0, NI_NUMERICHOST);
    if (rc != 0) {
        copy_cstr(dst, dst_size, "unknown");
        return;
    }
    if (net_normalize_ip_literal(raw, dst, dst_size) != 0) {
        copy_cstr(dst, dst_size, raw);
    }
}

int query_server_run(const char *port, registry_t *registry)
{
    int listen_fd;

    if (port == NULL || registry == NULL) {
        errno = EINVAL;
        return -1;
    }

    listen_fd = net_listen(port, 16);
    if (listen_fd < 0) {
        perror("net_listen");
        return -1;
    }

    server_log("p2p-server listening on port %s", port);

    for (;;) {
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);
        client_ctx_t *ctx;
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

        ctx = (client_ctx_t *)calloc(1u, sizeof(*ctx));
        if (ctx == NULL) {
            perror("calloc");
            net_close(client_fd);
            continue;
        }

        ctx->fd = client_fd;
        ctx->registry = registry;
        socket_ip_string((const struct sockaddr *)&addr, addr_len, ctx->peer_ip, sizeof(ctx->peer_ip));
        server_log("accepted connection from %s", ctx->peer_ip);

        if (pthread_create(&thread, NULL, client_thread_main, ctx) != 0) {
            perror("pthread_create");
            net_close(client_fd);
            free(ctx);
            continue;
        }
        (void)pthread_detach(thread);
    }
}
