#include "server/registry.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0u) {
        return;
    }
    if (src == NULL) {
        src = "";
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

int registry_init(registry_t *registry)
{
    if (registry == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(registry, 0, sizeof(*registry));
    return pthread_mutex_init(&registry->lock, NULL);
}

void registry_destroy(registry_t *registry)
{
    if (registry != NULL) {
        (void)pthread_mutex_destroy(&registry->lock);
    }
}

int registry_register_peer(registry_t *registry,
                           const char *ip,
                           uint16_t data_port,
                           const file_meta_t *files,
                           size_t file_count)
{
    size_t i;
    size_t slot = REGISTRY_MAX_PEERS;
    size_t copy_count;
    uint64_t now;

    if (registry == NULL || ip == NULL || (files == NULL && file_count > 0u)) {
        errno = EINVAL;
        return -1;
    }

    if (pthread_mutex_lock(&registry->lock) != 0) {
        return -1;
    }

    for (i = 0u; i < registry->peer_count; ++i) {
        if (strcmp(registry->peers[i].peer.ip, ip) == 0 &&
            registry->peers[i].peer.data_port == data_port) {
            slot = i;
            break;
        }
    }

    if (slot == REGISTRY_MAX_PEERS) {
        if (registry->peer_count >= REGISTRY_MAX_PEERS) {
            (void)pthread_mutex_unlock(&registry->lock);
            errno = ENOSPC;
            return -1;
        }
        slot = registry->peer_count;
        registry->peer_count++;
    }

    now = (uint64_t)time(NULL);
    copy_string(registry->peers[slot].peer.ip, sizeof(registry->peers[slot].peer.ip), ip);
    registry->peers[slot].peer.data_port = data_port;
    registry->peers[slot].peer.last_seen_epoch = now;

    copy_count = file_count > P2P_MAX_FILES_PER_PEER ? P2P_MAX_FILES_PER_PEER : file_count;
    registry->peers[slot].file_count = copy_count;
    for (i = 0u; i < copy_count; ++i) {
        registry->peers[slot].files[i] = files[i];
        copy_string(registry->peers[slot].files[i].owner_ip,
                    sizeof(registry->peers[slot].files[i].owner_ip),
                    ip);
        registry->peers[slot].files[i].owner_port = data_port;
    }

    return pthread_mutex_unlock(&registry->lock);
}

size_t registry_find_by_name(registry_t *registry,
                             const char *term,
                             file_meta_t *results,
                             size_t capacity)
{
    size_t copied = 0u;
    size_t i;

    if (registry == NULL || term == NULL || (results == NULL && capacity > 0u)) {
        errno = EINVAL;
        return 0u;
    }

    if (pthread_mutex_lock(&registry->lock) != 0) {
        return 0u;
    }

    for (i = 0u; i < registry->peer_count && copied < capacity; ++i) {
        size_t j;
        for (j = 0u; j < registry->peers[i].file_count && copied < capacity; ++j) {
            if (strstr(registry->peers[i].files[j].name, term) != NULL) {
                results[copied++] = registry->peers[i].files[j];
            }
        }
    }

    (void)pthread_mutex_unlock(&registry->lock);
    return copied;
}

size_t registry_find_by_identity(registry_t *registry,
                                 uint64_t hash,
                                 uint64_t size_bytes,
                                 file_meta_t *results,
                                 size_t capacity)
{
    size_t copied = 0u;
    size_t i;

    if (registry == NULL || (results == NULL && capacity > 0u)) {
        errno = EINVAL;
        return 0u;
    }

    if (pthread_mutex_lock(&registry->lock) != 0) {
        return 0u;
    }

    for (i = 0u; i < registry->peer_count && copied < capacity; ++i) {
        size_t j;
        for (j = 0u; j < registry->peers[i].file_count && copied < capacity; ++j) {
            const file_meta_t *file = &registry->peers[i].files[j];
            if (file->hash == hash && file->size_bytes == size_bytes) {
                results[copied++] = *file;
            }
        }
    }

    (void)pthread_mutex_unlock(&registry->lock);
    return copied;
}

size_t registry_recent_peers(registry_t *registry,
                             peer_entry_t *peers,
                             size_t capacity)
{
    return registry_recent_peers_except(registry, NULL, 0u, peers, capacity);
}

size_t registry_recent_peers_except(registry_t *registry,
                                    const char *exclude_ip,
                                    uint16_t exclude_port,
                                    peer_entry_t *peers,
                                    size_t capacity)
{
    size_t copied = 0u;
    size_t i;

    if (registry == NULL || (peers == NULL && capacity > 0u)) {
        errno = EINVAL;
        return 0u;
    }

    if (pthread_mutex_lock(&registry->lock) != 0) {
        return 0u;
    }

    i = registry->peer_count;
    while (i > 0u && copied < capacity) {
        --i;
        if (exclude_ip != NULL &&
            strcmp(registry->peers[i].peer.ip, exclude_ip) == 0 &&
            registry->peers[i].peer.data_port == exclude_port) {
            continue;
        }
        peers[copied++] = registry->peers[i].peer;
    }

    (void)pthread_mutex_unlock(&registry->lock);
    return copied;
}
