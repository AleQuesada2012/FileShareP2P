#include "search/flood.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "common/net.h"
#include "common/protocol.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include "search/aggregator.h"
#include "client/scanner.h"
#include <stddef.h>

#define MAX_SEEN_QUERIES 1000
#define QUERY_EXPIRY_SEC 60

neighbor_list_t global_neighbors;

extern response_aggregator_t global_aggregator;

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

void flood_register_query_id(const char *query_id)
{
    (void)check_and_register_query(query_id);
}

static int send_tcp_frame(const char *ip, uint16_t port, uint16_t opcode, const void *payload, uint32_t payload_size)
{
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    int fd = net_connect(ip, port_str);
    if (fd < 0) return -1;

    p2p_msg_header_t header;
    memset(&header, 0, sizeof(header));
    header.opcode = htons(opcode);
    header.version = htons(P2P_PROTOCOL_VERSION);
    header.payload_len = htonl(payload_size);


    uint32_t total_size = sizeof(p2p_msg_header_t) + payload_size;
    uint8_t *buffer = (uint8_t *)malloc(total_size);
    int rc;

    if (buffer == NULL) {
        net_close(fd);
        errno = ENOMEM;
        return -1;
    }

    memcpy(buffer, &header, sizeof(p2p_msg_header_t));
    if (payload_size > 0 && payload != NULL) {
        memcpy(buffer + sizeof(p2p_msg_header_t), payload, payload_size);
    }

    rc = net_send_msg(fd, buffer, total_size);
    free(buffer);
    net_close(fd);
    return rc;
}


void flood_handle_message(const uint8_t *buffer, uint32_t bytes_read, const flood_config_t *config)
{
    if (bytes_read >= sizeof(p2p_msg_header_t)) {
        p2p_msg_header_t *header = (p2p_msg_header_t *)buffer;
        uint16_t opcode = ntohs(header->opcode);

        // --- CASO A: ALGUIEN ESTÁ BUSCANDO UN ARCHIVO ---
        if (opcode == P2P_MSG_QUERY_FLOOD) {
            query_msg_t *incoming_query = (query_msg_t *)(buffer + sizeof(p2p_msg_header_t));

            if (check_and_register_query(incoming_query->query_id)) {
                {
                    peer_entry_t origin_peer;
                    memset(&origin_peer, 0, sizeof(origin_peer));
                    strncpy(origin_peer.ip, incoming_query->origin_ip, P2P_MAX_IP_LEN - 1);
                    origin_peer.data_port = ntohs(incoming_query->origin_port);
                    if (origin_peer.ip[0] != '\0' && origin_peer.data_port != 0) {
                        neighbors_add(&global_neighbors, &origin_peer);
                    }
                }
                if (access(config->share_folder, R_OK) == 0) {
                    query_result_t result_msg;
                    memset(&result_msg, 0, sizeof(result_msg));
                    strncpy(result_msg.query_id, incoming_query->query_id, P2P_MAX_QUERY_ID);

                    uint32_t match_count = 0;

                    for (size_t i = 0; i < config->shared_file_count && match_count < P2P_MAX_RESULTS; i++) {

                        if (strstr(config->shared_files[i].name, incoming_query->term) != NULL) {
                            file_meta_t matched_file = config->shared_files[i];

                            strncpy(matched_file.owner_ip, config->node_ip, P2P_MAX_IP_LEN - 1);
                            matched_file.owner_port = htons(config->data_port);

                            matched_file.hash = host_to_net64(matched_file.hash);
                            matched_file.size_bytes = host_to_net64(matched_file.size_bytes);

                            result_msg.results[match_count] = matched_file;
                            match_count++;
                        }
                    }

                    if (match_count > 0) {
                        result_msg.result_count = htonl(match_count);

                        uint32_t payload_size = offsetof(query_result_t, results) +
                                                (match_count * sizeof(file_meta_t));

                        printf("[P2P Local] ¡Encontré %u coincidencia(s)! Enviando resultado a %s:%u\n",
                               match_count, incoming_query->origin_ip, ntohs(incoming_query->origin_port));

                        send_tcp_frame(incoming_query->origin_ip, ntohs(incoming_query->origin_port),
                                       P2P_MSG_QUERY_RESULT, &result_msg, payload_size);
                    }
                } else {
                    fprintf(stderr,
                            "distributed search: share folder unavailable (%s); continuing without local results\n",
                            config->share_folder);
                }

                peer_entry_t sender;
                memset(&sender, 0, sizeof(sender));

                flood_forward_query(incoming_query, &sender);
            }
        }
        else if (opcode == P2P_MSG_QUERY_RESULT) {

            query_result_t *incoming_result = (query_result_t *)(buffer + sizeof(p2p_msg_header_t));
            uint32_t count = ntohl(incoming_result->result_count);

            for (uint32_t i = 0; i < count; i++) {
                file_meta_t received_file = incoming_result->results[i];
                received_file.owner_port = ntohs(received_file.owner_port);

                received_file.hash = net_to_host64(received_file.hash);
                received_file.size_bytes = net_to_host64(received_file.size_bytes);

                aggregator_add(&global_aggregator, &received_file);
            }
        }
    }
}

int flood_forward_query(const query_msg_t *query, const peer_entry_t *sender)
{
    if (query == NULL || sender == NULL) {
        errno = EINVAL;
        return -1;
    }

    query_msg_t query_to_forward = *query;
    int max_ttl = P2P_MAX_TTL;

    if (query_to_forward.ttl > max_ttl) {
        query_to_forward.ttl = max_ttl;
    }

    if (query_to_forward.ttl <= 0) {
        return 0;
    }

    query_to_forward.ttl -= 1;

    peer_entry_t active_peers[P2P_MAX_NEIGHBORS];
    size_t count = neighbors_snapshot(&global_neighbors, active_peers, P2P_MAX_NEIGHBORS);

    for (size_t i = 0; i < count; ++i) {
        // NO reenviar al nodo que nos acaba de mandar el mensaje
        if (strcmp(active_peers[i].ip, sender->ip) == 0 &&
            active_peers[i].data_port == sender->data_port) {
            continue;
            }
        uint16_t Dport = active_peers[i].data_port;

        send_tcp_frame(active_peers[i].ip, Dport,
                       P2P_MSG_QUERY_FLOOD, &query_to_forward, sizeof(query_msg_t));
    }

    return 0;
}
