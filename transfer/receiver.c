#include "transfer/receiver.h"

#include "common/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define TRANSFER_RECV_CHUNK_SIZE 16384u

typedef struct {
    p2p_msg_header_t header;
    transfer_req_t payload;
} transfer_req_frame_t;

typedef struct {
    p2p_msg_header_t header;
    transfer_data_header_t data_header;
    unsigned char data[TRANSFER_RECV_CHUNK_SIZE];
} transfer_data_frame_t;

typedef struct {
    int fd;
    pthread_mutex_t lock;
    int failed;
} assembly_state_t;

typedef struct {
    uint64_t hash;
    uint64_t size;
    uint64_t byte_start;
    uint64_t byte_end;
    peer_entry_t peer;
    assembly_state_t *assembly;
} segment_request_t;



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

static int write_all_at(int fd, const unsigned char *data, size_t len, uint64_t offset)
{
    size_t written_total = 0u;

    while (written_total < len) {
        ssize_t written = pwrite(fd,
                                 data + written_total,
                                 len - written_total,
                                 (off_t)(offset + (uint64_t)written_total));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        written_total += (size_t)written;
    }

    return 0;
}

static void mark_failed(assembly_state_t *assembly)
{
    if (pthread_mutex_lock(&assembly->lock) == 0) {
        assembly->failed = 1;
        (void)pthread_mutex_unlock(&assembly->lock);
    }
}

static int send_transfer_request(int fd, const segment_request_t *segment)
{
    transfer_req_frame_t frame;

    memset(&frame, 0, sizeof(frame));
    encode_header(&frame.header, P2P_MSG_TRANSFER_REQ, (uint32_t)sizeof(frame.payload));
    frame.payload.hash = host_to_net64(segment->hash);
    frame.payload.size = host_to_net64(segment->size);
    frame.payload.byte_start = host_to_net64(segment->byte_start);
    frame.payload.byte_end = host_to_net64(segment->byte_end);

    return net_send_msg(fd, &frame, (uint32_t)sizeof(frame));
}

static int receive_segment_frames(int fd, const segment_request_t *segment)
{
    uint64_t expected_len = segment->byte_end - segment->byte_start + 1u;
    uint64_t received_total = 0u;

    while (received_total < expected_len) {
        transfer_data_frame_t frame;
        uint32_t frame_len = 0u;
        uint32_t payload_len = 0u;
        uint16_t opcode = 0u;
        uint64_t frame_hash;
        uint64_t frame_size;
        uint64_t frame_start;
        uint32_t data_len;
        int rc;

        memset(&frame, 0, sizeof(frame));
        rc = net_recv_msg(fd, &frame, sizeof(frame), &frame_len);
        if (rc <= 0) {
            errno = rc == 0 ? ECONNRESET : errno;
            return -1;
        }
        if (frame_len < sizeof(frame.header) + sizeof(frame.data_header) ||
            decode_header(&frame.header, &opcode, &payload_len) != 0 ||
            opcode != (uint16_t)P2P_MSG_TRANSFER_DATA ||
            payload_len != frame_len - (uint32_t)sizeof(frame.header)) {
            errno = EPROTO;
            return -1;
        }

        frame_hash = net_to_host64(frame.data_header.hash);
        frame_size = net_to_host64(frame.data_header.size);
        frame_start = net_to_host64(frame.data_header.byte_start);
        data_len = ntohl(frame.data_header.data_len);
        if (frame_hash != segment->hash ||
            frame_size != segment->size ||
            data_len == 0u ||
            data_len > TRANSFER_RECV_CHUNK_SIZE ||
            sizeof(frame.data_header) + (size_t)data_len != payload_len ||
            frame_start < segment->byte_start ||
            frame_start > segment->byte_end ||
            frame_start != segment->byte_start + received_total ||
            (uint64_t)data_len > segment->byte_end - frame_start + 1u) {
            errno = EPROTO;
            return -1;
        }

        if (write_all_at(segment->assembly->fd, frame.data, data_len, frame_start) != 0) {
            return -1;
        }
        received_total += (uint64_t)data_len;
    }

    return 0;
}

static void *segment_thread_main(void *arg)
{
    segment_request_t *segment = (segment_request_t *)arg;
    char port[16];
    int fd;

    (void)snprintf(port, sizeof(port), "%u", (unsigned)segment->peer.data_port);
    fd = net_connect(segment->peer.ip, port);
    if (fd < 0) {
        fprintf(stderr,
                "transfer: could not connect to peer %s:%u for bytes %llu-%llu: %s\n",
                segment->peer.ip,
                (unsigned)segment->peer.data_port,
                (unsigned long long)segment->byte_start,
                (unsigned long long)segment->byte_end,
                strerror(errno));
        mark_failed(segment->assembly);
        return NULL;
    }

    if (send_transfer_request(fd, segment) != 0 ||
        receive_segment_frames(fd, segment) != 0) {
        fprintf(stderr,
                "transfer: segment %llu-%llu from %s:%u failed: %s\n",
                (unsigned long long)segment->byte_start,
                (unsigned long long)segment->byte_end,
                segment->peer.ip,
                (unsigned)segment->peer.data_port,
                strerror(errno));
        mark_failed(segment->assembly);
    }

    net_close(fd);
    return NULL;
}

int transfer_receive_segment(int peer_fd,
                             int output_fd,
                             uint64_t hash,
                             uint64_t size,
                             uint64_t byte_start,
                             uint64_t byte_end)
{
    assembly_state_t assembly;
    segment_request_t segment;

    if (peer_fd < 0 || output_fd < 0 || size == 0u || byte_start > byte_end || byte_end >= size) {
        errno = EINVAL;
        return -1;
    }

    memset(&assembly, 0, sizeof(assembly));
    assembly.fd = output_fd;
    memset(&segment, 0, sizeof(segment));
    segment.hash = hash;
    segment.size = size;
    segment.byte_start = byte_start;
    segment.byte_end = byte_end;
    segment.assembly = &assembly;

    return receive_segment_frames(peer_fd, &segment);
}

static const char *extension_from_name(const char *source_name)
{
    const char *name;
    const char *dot;

    if (source_name == NULL || source_name[0] == '\0') {
        return "";
    }

    name = strrchr(source_name, '/');
    name = name == NULL ? source_name : name + 1;
    dot = strrchr(name, '.');
    if (dot == NULL || dot == name || dot[1] == '\0') {
        return "";
    }

    return dot;
}

static int build_download_paths(char *final_path,
                                size_t final_path_size,
                                char *partial_path,
                                size_t partial_path_size,
                                const char *destination_folder,
                                const char *source_name,
                                uint64_t hash,
                                uint64_t size)
{
    int final_len;
    int partial_len;
    const char *extension = extension_from_name(source_name);

    final_len = snprintf(final_path,
                         final_path_size,
                         "%s/download_%llu_%llu%s",
                         destination_folder,
                         (unsigned long long)size,
                         (unsigned long long)hash,
                         extension);
    if (final_len < 0 || final_len >= (int)final_path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    partial_len = snprintf(partial_path, partial_path_size, "%s.part", final_path);
    if (partial_len < 0 || partial_len >= (int)partial_path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

int transfer_request(uint64_t hash,
                     uint64_t size,
                     const peer_entry_t *peers,
                     size_t peer_count,
                     const char *source_name,
                     const char *destination_folder)
{
    assembly_state_t assembly;
    segment_request_t *segments = NULL;
    pthread_t *threads = NULL;
    char final_path[PATH_MAX];
    char partial_path[PATH_MAX];
    size_t active_count;
    size_t started = 0u;
    uint64_t base_len;
    int rc = -1;
    size_t i;

    if (size == 0u || peers == NULL || peer_count == 0u || destination_folder == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (access(destination_folder, W_OK) != 0) {
        fprintf(stderr,
                "transfer: destination folder unavailable (%s): %s\n",
                destination_folder,
                strerror(errno));
        return -1;
    }
    if (build_download_paths(final_path,
                             sizeof(final_path),
                             partial_path,
                             sizeof(partial_path),
                             destination_folder,
                             source_name,
                             hash,
                             size) != 0) {
        return -1;
    }

    active_count = peer_count;
    if ((uint64_t)active_count > size) {
        active_count = (size_t)size;
    }
    base_len = size / (uint64_t)active_count;
    printf("Requesting S=%llu H=%llu from %zu peer(s) as %zu segment(s).\n",
           (unsigned long long)size,
           (unsigned long long)hash,
           peer_count,
           active_count);

    segments = (segment_request_t *)calloc(active_count, sizeof(*segments));
    threads = (pthread_t *)calloc(active_count, sizeof(*threads));
    if (segments == NULL || threads == NULL) {
        free(segments);
        free(threads);
        errno = ENOMEM;
        return -1;
    }

    memset(&assembly, 0, sizeof(assembly));
    if (pthread_mutex_init(&assembly.lock, NULL) != 0) {
        free(segments);
        free(threads);
        return -1;
    }
    assembly.fd = open(partial_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (assembly.fd < 0) {
        (void)pthread_mutex_destroy(&assembly.lock);
        free(segments);
        free(threads);
        return -1;
    }
    if (ftruncate(assembly.fd, (off_t)size) != 0) {
        goto cleanup;
    }

    for (i = 0u; i < active_count; ++i) {
        uint64_t start = (uint64_t)i * base_len;
        uint64_t end = i + 1u == active_count ? size - 1u : start + base_len - 1u;

        segments[i].hash = hash;
        segments[i].size = size;
        segments[i].byte_start = start;
        segments[i].byte_end = end;
        segments[i].peer = peers[i];
        segments[i].assembly = &assembly;
        printf("  segment %zu: bytes %llu-%llu from %s:%u\n",
               i + 1u,
               (unsigned long long)start,
               (unsigned long long)end,
               peers[i].ip,
               (unsigned)peers[i].data_port);

        if (pthread_create(&threads[i], NULL, segment_thread_main, &segments[i]) != 0) {
            fprintf(stderr,
                    "transfer: could not start segment thread %zu: %s\n",
                    i + 1u,
                    strerror(errno));
            mark_failed(&assembly);
            break;
        }
        started++;
    }

    for (i = 0u; i < started; ++i) {
        (void)pthread_join(threads[i], NULL);
    }

    if (assembly.failed || started != active_count) {
        errno = EIO;
        goto cleanup;
    }
    if (fsync(assembly.fd) != 0) {
        goto cleanup;
    }
    if (close(assembly.fd) != 0) {
        assembly.fd = -1;
        goto cleanup_after_close;
    }
    assembly.fd = -1;
    if (rename(partial_path, final_path) != 0) {
        goto cleanup_after_close;
    }

    printf("Downloaded file to %s\n", final_path);
    rc = 0;

cleanup:
    if (assembly.fd >= 0) {
        (void)close(assembly.fd);
    }
cleanup_after_close:
    if (rc != 0) {
        (void)unlink(partial_path);
    }
    (void)pthread_mutex_destroy(&assembly.lock);
    free(segments);
    free(threads);
    return rc;
}
