#include "search/neighbors.h"
#include "search/aggregator.h"
#include "search/flood.h"

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

static void generate_query_id(char *buffer) {
    const char *hex = "0123456789abcdef";
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    for (unsigned int i = 0; i < P2P_MAX_QUERY_ID - 1; i++) {
        buffer[i] = hex[rand() % 16];
    }
    buffer[P2P_MAX_QUERY_ID - 1] = '\0';
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

    // (Opcional) Si en el futuro sacas la IP y puerto del context, reemplazas esto:
    strncpy(query.origin_ip, global_flood_config.node_ip, P2P_MAX_IP_LEN - 1);
    query.origin_port = htons(global_flood_config.listen_port);
    query.ttl = ttl;

    // Crear sender vacío para la propagación
    peer_entry_t dummy_sender;
    memset(&dummy_sender, 0, sizeof(dummy_sender));

    // Propagar a la red a través de tu módulo flood
    if (flood_forward_query(&query, &dummy_sender) != 0) {
        return -1;
    }

    // Congelar este hilo el tiempo exacto del timeout (usleep usa microsegundos)
    usleep(timeout_ms * 1000);

    // Finalizó el tiempo. Recolectamos todo lo que el hilo receptor haya depositado
    aggregator_collect(&global_aggregator, results_out);

    return 0;
}
