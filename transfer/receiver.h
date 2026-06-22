#ifndef FS_P2P_TRANSFER_RECEIVER_H
#define FS_P2P_TRANSFER_RECEIVER_H

#include "common/protocol.h"

#include <stddef.h>
#include <stdint.h>

int transfer_receive_segment(int peer_fd,
                             int output_fd,
                             uint64_t hash,
                             uint64_t size,
                             uint64_t byte_start,
                             uint64_t byte_end);

int transfer_request(uint64_t hash,
                     uint64_t size,
                     const peer_entry_t *peers,
                     size_t peer_count,
                     const char *source_name,
                     const char *destination_folder);

#endif
