#ifndef FS_P2P_SEARCH_NEIGHBORS_H
#define FS_P2P_SEARCH_NEIGHBORS_H

#include "common/protocol.h"

#include <stddef.h>
#include <pthread.h>

typedef struct {
    pthread_mutex_t lock;
    size_t count;
    peer_entry_t peers[P2P_MAX_NEIGHBORS];
} neighbor_list_t;

typedef struct {
    size_t count;
    file_meta_t items[P2P_MAX_RESULTS];
} search_results_t;

int neighbors_init(neighbor_list_t *neighbors);
void neighbors_destroy(neighbor_list_t *neighbors);
int neighbors_add(neighbor_list_t *neighbors, const peer_entry_t *peer);
size_t neighbors_snapshot(neighbor_list_t *neighbors, peer_entry_t *out, size_t capacity);
int search_distributed(const char *term, uint8_t ttl, unsigned timeout_ms, search_results_t *results_out);

#endif
