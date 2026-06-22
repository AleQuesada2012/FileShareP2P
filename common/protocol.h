#ifndef FS_P2P_COMMON_PROTOCOL_H
#define FS_P2P_COMMON_PROTOCOL_H

#include <stdint.h>
#define P2P_PROTOCOL_VERSION 1u
#define P2P_MAX_FILENAME 256u
#define P2P_MAX_IP_LEN 46u
#define P2P_MAX_QUERY_ID 17u
#define P2P_MAX_TERM 256u
#define P2P_MAX_FILES_PER_PEER 256u
#define P2P_MAX_RESULTS 256u
#define P2P_MAX_NEIGHBORS 16u
#define P2P_MAX_ERROR_MESSAGE 256u
#define P2P_DEFAULT_TTL 4u
#define P2P_MAX_TTL 6u
#define P2P_DEFAULT_SEARCH_TIMEOUT_MS 2000u

/*
 * Protocol note:
 * - TCP messages use net_send_msg/net_recv_msg as the outer length-prefixed
 *   frame. The frame payload always starts with p2p_msg_header_t, followed by
 *   the opcode-specific payload listed below.
 * - p2p_msg_header_t.payload_len is the number of bytes after the header.
 * - All multi-byte integer fields must be encoded in network byte order before
 *   transmission and decoded back to host byte order after receipt.
 * - Fixed-size character arrays are always null-terminated when populated.
 * - Do not transmit uninitialized padding bytes. Serialization code must zero
 *   structs before populating them or copy fields into an explicit byte buffer.
 * - register_resp_t.neighbors must exclude the registering peer.
 * - peer_entry_t.last_seen_epoch is seconds since the Unix epoch and is used for
 *   registry freshness/order; receivers may ignore it for neighbor seeding.
 * - The first iteration uses TCP for control, distributed search, and transfer
 *   messages.
 */
typedef enum {
    P2P_MSG_REGISTER_REQ = 1,
    P2P_MSG_REGISTER_RESP = 2,
    P2P_MSG_FIND_REQ = 3,
    P2P_MSG_FIND_RESP = 4,
    P2P_MSG_TRANSFER_REQ = 5,
    P2P_MSG_TRANSFER_DATA = 6,
    P2P_MSG_QUERY_FLOOD = 7,
    P2P_MSG_QUERY_RESULT = 8,
    P2P_MSG_ERROR = 255
} p2p_opcode_t;

typedef struct {
    uint16_t opcode;
    uint16_t version;
    uint32_t payload_len;
} p2p_msg_header_t;

typedef struct {
    char name[P2P_MAX_FILENAME];
    uint64_t hash;
    uint64_t size_bytes;
    char owner_ip[P2P_MAX_IP_LEN];
    uint16_t owner_port;
} file_meta_t;

typedef struct {
    char ip[P2P_MAX_IP_LEN];
    uint16_t data_port;
    uint64_t last_seen_epoch;
} peer_entry_t;

typedef struct {
    uint16_t data_port;
    uint32_t file_count;
    file_meta_t files[P2P_MAX_FILES_PER_PEER];
} register_req_t;

typedef struct {
    uint32_t neighbor_count;
    peer_entry_t neighbors[P2P_MAX_NEIGHBORS];
} register_resp_t;

typedef struct {
    char term[P2P_MAX_TERM];
} find_req_t;

typedef struct {
    uint32_t result_count;
    file_meta_t results[P2P_MAX_RESULTS];
} find_resp_t;

typedef struct {
    uint64_t hash;
    uint64_t size;
    uint64_t byte_start;
    uint64_t byte_end;
} transfer_req_t;

typedef struct {
    uint64_t hash;
    uint64_t size;
    uint64_t byte_start;
    uint32_t data_len;
} transfer_data_header_t;

typedef struct {
    char query_id[P2P_MAX_QUERY_ID];
    char term[P2P_MAX_TERM];
    char origin_ip[P2P_MAX_IP_LEN];
    uint16_t origin_port;
    uint8_t ttl;
} query_msg_t;

typedef struct {
    char query_id[P2P_MAX_QUERY_ID];
    uint32_t result_count;
    file_meta_t results[P2P_MAX_RESULTS];
} query_result_t;

typedef enum {
    P2P_ERROR_BAD_REQUEST = 1,
    P2P_ERROR_UNSUPPORTED_VERSION = 2,
    P2P_ERROR_UNKNOWN_OPCODE = 3,
    P2P_ERROR_REGISTRY_FULL = 4,
    P2P_ERROR_INTERNAL = 5
} p2p_error_code_t;

typedef struct {
    uint32_t code;
    char message[P2P_MAX_ERROR_MESSAGE];
} error_msg_t;

#endif
