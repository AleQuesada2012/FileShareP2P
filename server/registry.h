#ifndef FS_P2P_SERVER_REGISTRY_H
#define FS_P2P_SERVER_REGISTRY_H

#include "common/protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#define REGISTRY_MAX_PEERS 64u

typedef struct {
    peer_entry_t peer;
    size_t file_count;
    file_meta_t files[P2P_MAX_FILES_PER_PEER];
} registry_peer_t;

typedef struct {
    pthread_mutex_t lock;
    size_t peer_count;
    registry_peer_t peers[REGISTRY_MAX_PEERS];
} registry_t;

int registry_init(registry_t *registry);
void registry_destroy(registry_t *registry);
int registry_register_peer(registry_t *registry,
                           const char *ip,
                           uint16_t data_port,
                           const file_meta_t *files,
                           size_t file_count);
size_t registry_find_by_name(registry_t *registry,
                             const char *term,
                             file_meta_t *results,
                             size_t capacity);
size_t registry_recent_peers(registry_t *registry,
                             peer_entry_t *peers,
                             size_t capacity);

#endif
