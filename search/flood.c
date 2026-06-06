#include "search/flood.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include "common/net.h"
#include "common/protocol.h"
#include <sys/socket.h>
#include <arpa/inet.h>


#define MAX_SEEN_QUERIES 1000
#define QUERY_EXPIRY_SEC 60

typedef struct {
    char query_id[17];
    time_t expiry_time;
} seen_query_t;

static seen_query_t query_cache[MAX_SEEN_QUERIES];
static size_t cache_count = 0;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;


// Retorna true si la consulta es nueva (y la registra). False si ya se procesó.
static bool check_and_register_query(const char *query_id)
{
    time_t now = time(NULL);
    bool is_new = true;

    if (pthread_mutex_lock(&cache_lock) != 0) {
        return false; // Ante duda en concurrencia, rechazamos.
    }

    // Limpiar IDs expirados (lleva más de 60 segundos)
    size_t valid_count = 0;
    for (size_t i = 0; i < cache_count; ++i) {
        if (query_cache[i].expiry_time > now) {
            query_cache[valid_count] = query_cache[i];
            valid_count++;
        }
    }
    cache_count = valid_count;

    // Buscar si el ID ya existe
    for (size_t i = 0; i < cache_count; ++i) {
        if (strncmp(query_cache[i].query_id, query_id, 16) == 0) {
            is_new = false;
            break;
        }
    }

    // Si es nuevo, lo guardamos
    if (is_new && cache_count < MAX_SEEN_QUERIES) {
        strncpy(query_cache[cache_count].query_id, query_id, 16);
        query_cache[cache_count].query_id[16] = '\0';
        query_cache[cache_count].expiry_time = now + QUERY_EXPIRY_SEC;
        cache_count++;
    }

    (void)pthread_mutex_unlock(&cache_lock);
    return is_new;
}

// TODO
static void* flood_worker_thread(void* arg){
    (void)arg;
    return NULL;
}



int flood_listener_start(const flood_config_t *config, neighbor_list_t *neighbors)
{
    if (config == NULL || neighbors == NULL) {
        errno = EINVAL;
        return -1;
    }

    pthread_t listener_tid;

    if (pthread_create(&listener_tid, NULL, flood_worker_thread, NULL) != 0) {
        errno = EAGAIN;
        return -1;
    }

    // liberar recursos
    pthread_detach(listener_tid);

    return 0;
}

int flood_forward_query(const query_msg_t *query, const peer_entry_t *sender)
{
    if (query == NULL || sender == NULL) {
        errno = EINVAL;
        return -1;
    }

    query_msg_t query_to_forward = *query;

    int max_ttl = 4;

    if (query_to_forward.ttl > max_ttl) {
        query_to_forward.ttl = max_ttl;
    }

    //Detener si el TTL llega a cero
    if (query_to_forward.ttl <= 0) {
        return 0;
    }

    query_to_forward.ttl -= 1;


    extern neighbor_list_t global_neighbors;
    peer_entry_t active_peers[20]; // Capacidad máxima teórica
    size_t count = neighbors_snapshot(&global_neighbors, active_peers, 20);

    for (size_t i = 0; i < count; ++i) {
        // NO reenviar al nodo que nos acaba de mandar el mensaje
        if (strcmp(active_peers[i].ip, sender->ip) == 0 &&
            active_peers[i].data_port == sender->data_port) {
            continue;
        }

    }

    return 0;
}