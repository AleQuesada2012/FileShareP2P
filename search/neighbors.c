#include "search/neighbors.h"
#include "search/aggregator.h"
#include "search/flood.h"
#include "common/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

response_aggregator_t global_aggregator;
extern flood_config_t global_flood_config;

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
    peer_entry_t normalized_peer;
    char normalized_ip[P2P_MAX_IP_LEN];
    size_t i;

    if (neighbors == NULL || peer == NULL) {
        errno = EINVAL;
        return -1;
    }

    normalized_peer = *peer;
    normalized_peer.ip[P2P_MAX_IP_LEN - 1u] = '\0';
    if (net_normalize_ip_literal(normalized_peer.ip,
                                 normalized_ip,
                                 sizeof(normalized_ip)) == 0) {
        strncpy(normalized_peer.ip, normalized_ip, sizeof(normalized_peer.ip) - 1u);
        normalized_peer.ip[sizeof(normalized_peer.ip) - 1u] = '\0';
    } else {
        normalized_peer = *peer;
        normalized_peer.ip[P2P_MAX_IP_LEN - 1u] = '\0';
    }

    if (pthread_mutex_lock(&neighbors->lock) != 0) {
        return -1;
    }

    for (i = 0u; i < neighbors->count; ++i) {
        if (strcmp(neighbors->peers[i].ip, normalized_peer.ip) == 0 &&
            neighbors->peers[i].data_port == normalized_peer.data_port) {
            neighbors->peers[i] = normalized_peer;
            neighbors->peers[i].last_seen_epoch = (uint64_t)time(NULL);
            return pthread_mutex_unlock(&neighbors->lock);
        }
    }

    if (neighbors->count >= P2P_MAX_NEIGHBORS) {
        (void)pthread_mutex_unlock(&neighbors->lock);
        errno = ENOSPC;
        return -1;
    }

    neighbors->peers[neighbors->count] = normalized_peer;
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

static void generate_query_id(char *buffer) {
    const char *hex = "0123456789abcdef";
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    for (unsigned int i = 0; i < P2P_MAX_QUERY_ID - 1; i++) {
        buffer[i] = hex[rand() % 16];
    }
    buffer[P2P_MAX_QUERY_ID - 1] = '\0';
}

static void sleep_for_ms(unsigned timeout_ms)
{
    struct timespec remaining;

    remaining.tv_sec = (time_t)(timeout_ms / 1000u);
    remaining.tv_nsec = (long)(timeout_ms % 1000u) * 1000000L;

    while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {
    }
}

int search_distributed(const char *term, uint8_t ttl, unsigned timeout_ms, search_results_t *results_out)
{
    if (term == NULL || results_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Preparar el agregador global para una nueva búsqueda
    aggregator_destroy(&global_aggregator); // Limpiamos por si había basura de otra búsqueda
    aggregator_init(&global_aggregator);

    query_msg_t query;
    memset(&query, 0, sizeof(query));
    generate_query_id(query.query_id);
    strncpy(query.term, term, P2P_MAX_TERM - 1);

    strncpy(query.origin_ip, global_flood_config.node_ip, P2P_MAX_IP_LEN - 1);
    query.origin_port = htons(global_flood_config.listen_port);
    query.ttl = ttl;

    flood_register_query_id(query.query_id);

    // Crear sender vacío para la propagación
    peer_entry_t dummy_sender;
    memset(&dummy_sender, 0, sizeof(dummy_sender));

    if (flood_forward_query(&query, &dummy_sender) != 0) {
        return -1;
    }

    sleep_for_ms(timeout_ms);

    aggregator_collect(&global_aggregator, results_out);


    size_t write_idx = 0u;
    for (size_t i = 0u; i < results_out->count; ++i) {
        if (strcmp(results_out->items[i].owner_ip, global_flood_config.node_ip) == 0 &&
            results_out->items[i].owner_port == global_flood_config.data_port) {
            continue; // Saltar archivos propios
        }
        if (write_idx != i) {
            results_out->items[write_idx] = results_out->items[i];
        }
        write_idx++;
    }
    results_out->count = write_idx;

    return 0;
}
