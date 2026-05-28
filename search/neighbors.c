#include "search/neighbors.h"

#include <errno.h>
#include <string.h>
#include <time.h>

int neighbors_init(neighbor_list_t *neighbors)
{
    if (neighbors == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(neighbors, 0, sizeof(*neighbors));
    return pthread_mutex_init(&neighbors->lock, NULL);
}

void neighbors_destroy(neighbor_list_t *neighbors)
{
    if (neighbors != NULL) {
        (void)pthread_mutex_destroy(&neighbors->lock);
    }
}

int neighbors_add(neighbor_list_t *neighbors, const peer_entry_t *peer)
{
    size_t i;

    if (neighbors == NULL || peer == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (pthread_mutex_lock(&neighbors->lock) != 0) {
        return -1;
    }

    for (i = 0u; i < neighbors->count; ++i) {
        if (strcmp(neighbors->peers[i].ip, peer->ip) == 0 &&
            neighbors->peers[i].data_port == peer->data_port) {
            neighbors->peers[i] = *peer;
            neighbors->peers[i].last_seen_epoch = (uint64_t)time(NULL);
            return pthread_mutex_unlock(&neighbors->lock);
        }
    }

    if (neighbors->count >= P2P_MAX_NEIGHBORS) {
        (void)pthread_mutex_unlock(&neighbors->lock);
        errno = ENOSPC;
        return -1;
    }

    neighbors->peers[neighbors->count] = *peer;
    neighbors->peers[neighbors->count].last_seen_epoch = (uint64_t)time(NULL);
    neighbors->count++;

    return pthread_mutex_unlock(&neighbors->lock);
}

size_t neighbors_snapshot(neighbor_list_t *neighbors, peer_entry_t *out, size_t capacity)
{
    size_t copied;

    if (neighbors == NULL || (out == NULL && capacity > 0u)) {
        errno = EINVAL;
        return 0u;
    }

    if (pthread_mutex_lock(&neighbors->lock) != 0) {
        return 0u;
    }

    copied = neighbors->count < capacity ? neighbors->count : capacity;
    if (copied > 0u) {
        memcpy(out, neighbors->peers, copied * sizeof(out[0]));
    }

    (void)pthread_mutex_unlock(&neighbors->lock);
    return copied;
}

int search_distributed(const char *term, search_results_t *results_out)
{
    if (term == NULL || results_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(results_out, 0, sizeof(*results_out));
    errno = ENOSYS;
    return -1;
}
