#ifndef FS_P2P_SEARCH_FLOOD_H
#define FS_P2P_SEARCH_FLOOD_H

#include "common/protocol.h"
#include "search/neighbors.h"

#include <stdint.h>

typedef struct {
    uint8_t default_ttl;
    unsigned response_window_ms;
} flood_config_t;

int flood_listener_start(const flood_config_t *config, neighbor_list_t *neighbors);
int flood_forward_query(const query_msg_t *query, const peer_entry_t *sender);
int flood_handle_query_flood(const char *sender_ip, const query_msg_t *wire_query);
int flood_handle_query_result(const char *sender_ip, const query_result_t *wire_result);

void flood_query_cache_reset(void);
int flood_query_cache_note(const char *query_id, uint64_t now_epoch);
uint8_t flood_forward_ttl(uint8_t ttl);

#endif
