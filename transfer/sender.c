#include "transfer/sender.h"

#include "common/hash.h"
#include "common/net.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define TRANSFER_CHUNK_SIZE 16384u

typedef struct {
    p2p_msg_header_t header;
    transfer_data_header_t data_header;
    unsigned char data[TRANSFER_CHUNK_SIZE];
} transfer_data_frame_t;



static void encode_header(p2p_msg_header_t *header, p2p_opcode_t opcode, uint32_t payload_len)
{
    memset(header, 0, sizeof(*header));
    header->opcode = htons((uint16_t)opcode);
    header->version = htons((uint16_t)P2P_PROTOCOL_VERSION);
    header->payload_len = htonl(payload_len);
}

static int validate_request_for_file(const char *path, const transfer_req_t *request)
{
    uint64_t hash;
    uint64_t size;

    if (path == NULL || request == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (request->byte_start > request->byte_end) {
        errno = EINVAL;
        return -1;
    }
    if (access(path, R_OK) != 0) {
        return -1;
    }
    if (hash_file_with_size(path, &hash, &size) != 0) {
        return -1;
    }
    if (hash != request->hash || size != request->size) {
        errno = EINVAL;
        return -1;
    }
    if (request->size == 0u ||
        request->byte_start >= request->size ||
        request->byte_end >= request->size) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int transfer_send_range(int client_fd, const char *path, const transfer_req_t *request)
{
    transfer_data_frame_t frame;
    FILE *file;
    uint64_t offset;
    uint64_t remaining;

    if (validate_request_for_file(path, request) != 0) {
        return -1;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    if (fseeko(file, (off_t)request->byte_start, SEEK_SET) != 0) {
        (void)fclose(file);
        return -1;
    }

    offset = request->byte_start;
    remaining = request->byte_end - request->byte_start + 1u;
    while (remaining > 0u) {
        size_t want = remaining > TRANSFER_CHUNK_SIZE ? TRANSFER_CHUNK_SIZE : (size_t)remaining;
        size_t nread = fread(frame.data, 1u, want, file);
        uint32_t payload_len;
        uint32_t frame_len;

        if (nread == 0u) {
            if (ferror(file)) {
                (void)fclose(file);
                return -1;
            }
            errno = EIO;
            (void)fclose(file);
            return -1;
        }

        payload_len = (uint32_t)(sizeof(frame.data_header) + nread);
        frame_len = (uint32_t)(sizeof(frame.header) + payload_len);
        encode_header(&frame.header, P2P_MSG_TRANSFER_DATA, payload_len);
        frame.data_header.hash = host_to_net64(request->hash);
        frame.data_header.size = host_to_net64(request->size);
        frame.data_header.byte_start = host_to_net64(offset);
        frame.data_header.data_len = htonl((uint32_t)nread);

        if (net_send_msg(client_fd, &frame, frame_len) != 0) {
            (void)fclose(file);
            return -1;
        }

        offset += (uint64_t)nread;
        remaining -= (uint64_t)nread;
    }

    return fclose(file);
}

static int find_matching_file_recursive(const char *folder,
                                        const transfer_req_t *request,
                                        char *path_out,
                                        size_t path_out_size)
{
    DIR *dir;
    struct dirent *entry;

    dir = opendir(folder);
    if (dir == NULL) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name) >= (int)sizeof(path)) {
            continue;
        }
        if (lstat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (find_matching_file_recursive(path, request, path_out, path_out_size) == 0) {
                (void)closedir(dir);
                return 0;
            }
        } else if (S_ISREG(st.st_mode)) {
            uint64_t hash;
            uint64_t size;

            if (hash_file_with_size(path, &hash, &size) == 0 &&
                hash == request->hash &&
                size == request->size) {
                (void)snprintf(path_out, path_out_size, "%s", path);
                (void)closedir(dir);
                return 0;
            }
        }
    }

    (void)closedir(dir);
    errno = ENOENT;
    return -1;
}

int transfer_send_matching_file(int client_fd, const char *share_folder, const transfer_req_t *request)
{
    char path[PATH_MAX];

    if (share_folder == NULL || request == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (access(share_folder, R_OK) != 0) {
        return -1;
    }
    if (find_matching_file_recursive(share_folder, request, path, sizeof(path)) != 0) {
        return -1;
    }

    return transfer_send_range(client_fd, path, request);
}
