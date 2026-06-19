#ifndef FS_P2P_SEARCH_FLOOD_H
#define FS_P2P_SEARCH_FLOOD_H

#include "common/protocol.h"
#include "search/neighbors.h"

#include <stdint.h>

typedef struct {
    uint8_t default_ttl;
    unsigned response_window_ms;
    uint16_t listen_port;
    uint16_t data_port;
    char node_ip[46];
    char share_folder[4096];
} flood_config_t;

void flood_handle_message(const uint8_t *buffer, uint32_t bytes_read, const flood_config_t *config);
int flood_forward_query(const query_msg_t *query, const peer_entry_t *sender);
void flood_register_query_id(const char *query_id);

#endif