#include "transfer/listener.h"

#include "common/net.h"
#include "common/protocol.h"
#include "transfer/sender.h"
#include "search/flood.h"

extern flood_config_t global_flood_config;

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char port[16];
    char share_folder[PATH_MAX];
} listener_context_t;

typedef struct {
    int client_fd;
    char share_folder[PATH_MAX];
} request_context_t;

typedef struct {
    p2p_msg_header_t header;
    transfer_req_t payload;
} transfer_req_frame_t;

typedef struct {
    p2p_msg_header_t header;
    error_msg_t payload;
} error_frame_t;

static int is_little_endian(void)
{
    const uint16_t value = 1u;
    return *((const unsigned char *)&value) == 1u;
}

static uint64_t listener_net_to_host64(uint64_t value)
{
    if (!is_little_endian()) {
        return value;
    }

    return ((uint64_t)ntohl((uint32_t)(value & UINT64_C(0xffffffff))) << 32) |
           (uint64_t)ntohl((uint32_t)(value >> 32));
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
    if (header == NULL || opcode_out == NULL || payload_len_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (ntohs(header->version) != (uint16_t)P2P_PROTOCOL_VERSION) {
        errno = EPROTO;
        return -1;
    }

    *opcode_out = ntohs(header->opcode);
    *payload_len_out = ntohl(header->payload_len);
    return 0;
}

static void decode_transfer_req(transfer_req_t *dst, const transfer_req_t *src)
{
    dst->hash = listener_net_to_host64(src->hash);
    dst->size = listener_net_to_host64(src->size);
    dst->byte_start = listener_net_to_host64(src->byte_start);
    dst->byte_end = listener_net_to_host64(src->byte_end);
}

static int send_error(int fd, p2p_error_code_t code, const char *message)
{
    error_frame_t frame;

    memset(&frame, 0, sizeof(frame));
    encode_header(&frame.header, P2P_MSG_ERROR, (uint32_t)sizeof(frame.payload));
    frame.payload.code = htonl((uint32_t)code);
    if (message != NULL) {
        (void)snprintf(frame.payload.message, sizeof(frame.payload.message), "%s", message);
    }

    return net_send_msg(fd, &frame, (uint32_t)sizeof(frame));
}

static int handle_transfer_request(int client_fd, const char *share_folder, const void *buffer, uint32_t frame_len)
{
    transfer_req_frame_t frame;
    transfer_req_t request;
    uint32_t payload_len = 0u;
    uint16_t opcode = 0u;

    if (frame_len != (uint32_t)sizeof(frame)) {
        (void)send_error(client_fd, P2P_ERROR_BAD_REQUEST, "invalid transfer request");
        errno = EPROTO;
        return -1;
    }
    memcpy(&frame, buffer, frame_len);

    if (decode_header(&frame.header, &opcode, &payload_len) != 0 ||
        opcode != (uint16_t)P2P_MSG_TRANSFER_REQ ||
        payload_len != sizeof(frame.payload)) {
        (void)send_error(client_fd, P2P_ERROR_BAD_REQUEST, "invalid transfer request");
        errno = EPROTO;
        return -1;
    }

    decode_transfer_req(&request, &frame.payload);
    if (transfer_send_matching_file(client_fd, share_folder, &request) != 0) {
        (void)send_error(client_fd, P2P_ERROR_INTERNAL, "requested range is unavailable");
        return -1;
    }

    return 0;
}

static void *request_thread_main(void *arg)
{
    request_context_t *ctx = (request_context_t *)arg;
    uint8_t buffer[8192];
    uint32_t bytes_read = 0;

    if (net_recv_msg(ctx->client_fd, buffer, sizeof(buffer), &bytes_read) == 1) {
        if (bytes_read >= sizeof(p2p_msg_header_t)) {
            p2p_msg_header_t *header = (p2p_msg_header_t *)buffer;
            uint16_t opcode = ntohs(header->opcode);

            if (opcode == P2P_MSG_TRANSFER_REQ) {
                (void)handle_transfer_request(ctx->client_fd, ctx->share_folder, buffer, bytes_read);
            } else if (opcode == P2P_MSG_QUERY_FLOOD || opcode == P2P_MSG_QUERY_RESULT) {
                flood_handle_message(buffer, bytes_read, &global_flood_config);
            } else {
                (void)send_error(ctx->client_fd, P2P_ERROR_BAD_REQUEST, "unknown opcode");
            }
        }
    }

    net_close(ctx->client_fd);
    free(ctx);
    return NULL;
}

static void *listener_thread_main(void *arg)
{
    listener_context_t *ctx = (listener_context_t *)arg;
    listener_context_t listener;
    int listen_fd;

    listener = *ctx;
    free(ctx);

    listen_fd = net_listen(listener.port, 16);
    if (listen_fd < 0) {
        perror("transfer net_listen");
        return NULL;
    }

    printf("transfer listener ready on port %s\n", listener.port);
    for (;;) {
        struct sockaddr_storage peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);
        request_context_t *request_ctx;
        pthread_t request_thread;
        int client_fd;

        client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("transfer accept");
            continue;
        }

        request_ctx = (request_context_t *)malloc(sizeof(*request_ctx));
        if (request_ctx == NULL) {
            perror("malloc");
            net_close(client_fd);
            continue;
        }

        request_ctx->client_fd = client_fd;
        (void)snprintf(request_ctx->share_folder, sizeof(request_ctx->share_folder), "%s", listener.share_folder);
        if (pthread_create(&request_thread, NULL, request_thread_main, request_ctx) != 0) {
            perror("pthread_create");
            net_close(client_fd);
            free(request_ctx);
            continue;
        }
        (void)pthread_detach(request_thread);
    }
}

int transfer_listener_start(const char *port, const char *share_folder)
{
    listener_context_t *ctx;
    pthread_t listener_thread;

    if (port == NULL || share_folder == NULL) {
        errno = EINVAL;
        return -1;
    }

    ctx = (listener_context_t *)malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    (void)snprintf(ctx->port, sizeof(ctx->port), "%s", port);
    (void)snprintf(ctx->share_folder, sizeof(ctx->share_folder), "%s", share_folder);

    if (pthread_create(&listener_thread, NULL, listener_thread_main, ctx) != 0) {
        free(ctx);
        return -1;
    }

    return pthread_detach(listener_thread);
}
