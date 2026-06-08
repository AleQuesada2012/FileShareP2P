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

int flood_listener_start(const flood_config_t *config, neighbor_list_t *neighbors);
int flood_forward_query(const query_msg_t *query, const peer_entry_t *sender);

#endif