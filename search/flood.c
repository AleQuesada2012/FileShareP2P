#include "search/flood.h"

#include "client/scanner.h"
#include "common/net.h"
#include "search/aggregator.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define FLOOD_SEEN_CAPACITY 256u
#define FLOOD_SEEN_TTL_SECONDS 60u

typedef struct {
    p2p_msg_header_t header;
    query_msg_t payload;
} query_frame_t;

typedef struct {
    p2p_msg_header_t header;
    query_result_t payload;
} query_result_frame_t;

typedef struct {
    char query_id[P2P_MAX_QUERY_ID];
    uint64_t expires_at;
} seen_query_t;

typedef struct {
    int initialized;
    char share_folder[PATH_MAX];
    char origin_ip[P2P_MAX_IP_LEN];
    uint16_t data_port;
    uint8_t default_ttl;
    unsigned response_window_ms;
    neighbor_list_t neighbors;
    response_aggregator_t aggregator;
    pthread_mutex_t active_lock;
    int active;
    char active_query_id[P2P_MAX_QUERY_ID];
} search_runtime_t;

static search_runtime_t g_runtime;
static pthread_mutex_t g_runtime_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_seen_lock = PTHREAD_MUTEX_INITIALIZER;
static seen_query_t g_seen[FLOOD_SEEN_CAPACITY];

static int is_little_endian(void)
{
    const uint16_t value = 1u;
    return *((const unsigned char *)&value) == 1u;
}

static uint64_t host_to_net64(uint64_t value)
{
    if (!is_little_endian()) {
        return value;
    }

    return ((uint64_t)htonl((uint32_t)(value & UINT64_C(0xffffffff))) << 32) |
           (uint64_t)htonl((uint32_t)(value >> 32));
}

static uint64_t net_to_host64(uint64_t value)
{
    return host_to_net64(value);
}

static void copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0u) {
        return;
    }
    if (src == NULL) {
        src = "";
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

static void encode_header(p2p_msg_header_t *header,
                          p2p_opcode_t opcode,
                          uint32_t payload_len)
{
    memset(header, 0, sizeof(*header));
    header->opcode = htons((uint16_t)opcode);
    header->version = htons((uint16_t)P2P_PROTOCOL_VERSION);
    header->payload_len = htonl(payload_len);
}

static void decode_query(query_msg_t *dst, const query_msg_t *src)
{
    *dst = *src;
    dst->query_id[P2P_MAX_QUERY_ID - 1u] = '\0';
    dst->term[P2P_MAX_TERM - 1u] = '\0';
    dst->origin_ip[P2P_MAX_IP_LEN - 1u] = '\0';
    dst->origin_port = ntohs(src->origin_port);
    if (dst->ttl > P2P_MAX_TTL) {
        dst->ttl = P2P_MAX_TTL;
    }
}

static void encode_query(query_msg_t *dst, const query_msg_t *src)
{
    memset(dst, 0, sizeof(*dst));
    copy_cstr(dst->query_id, sizeof(dst->query_id), src->query_id);
    copy_cstr(dst->term, sizeof(dst->term), src->term);
    copy_cstr(dst->origin_ip, sizeof(dst->origin_ip), src->origin_ip);
    dst->origin_port = htons(src->origin_port);
    dst->ttl = src->ttl;
}

static void encode_file_meta(file_meta_t *dst, const file_meta_t *src)
{
    memset(dst, 0, sizeof(*dst));
    copy_cstr(dst->name, sizeof(dst->name), src->name);
    dst->hash = host_to_net64(src->hash);
    dst->size_bytes = host_to_net64(src->size_bytes);
    copy_cstr(dst->owner_ip, sizeof(dst->owner_ip), src->owner_ip);
    dst->owner_port = htons(src->owner_port);
}

static void decode_file_meta(file_meta_t *dst, const file_meta_t *src)
{
    *dst = *src;
    dst->name[P2P_MAX_FILENAME - 1u] = '\0';
    dst->hash = net_to_host64(src->hash);
    dst->size_bytes = net_to_host64(src->size_bytes);
    dst->owner_ip[P2P_MAX_IP_LEN - 1u] = '\0';
    dst->owner_port = ntohs(src->owner_port);
}

static void detect_origin_ip(const char *server_ip, char *out, size_t out_size)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;

    copy_cstr(out, out_size, "127.0.0.1");
    if (server_ip == NULL || out == NULL || out_size == 0u) {
        return;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(server_ip, "9", &hints, &result) != 0) {
        return;
    }

    for (it = result; it != NULL; it = it->ai_next) {
        struct sockaddr_storage local_addr;
        socklen_t local_len = (socklen_t)sizeof(local_addr);
        int fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);

        if (fd < 0) {
            continue;
        }

        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0 &&
            getsockname(fd, (struct sockaddr *)&local_addr, &local_len) == 0 &&
            getnameinfo((struct sockaddr *)&local_addr,
                        local_len,
                        out,
                        (socklen_t)out_size,
                        NULL,
                        0,
                        NI_NUMERICHOST) == 0) {
            close(fd);
            break;
        }

        close(fd);
    }

    freeaddrinfo(result);
}

static void sleep_ms(unsigned ms)
{
    struct timespec remaining;
    struct timespec request;

    request.tv_sec = (time_t)(ms / 1000u);
    request.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&request, &remaining) != 0 && errno == EINTR) {
        request = remaining;
    }
}

static int generate_query_id(char *out, size_t out_size)
{
    uint64_t value = 0u;
    int fd;

    if (out == NULL || out_size < P2P_MAX_QUERY_ID) {
        errno = EINVAL;
        return -1;
    }

    fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t received = read(fd, &value, sizeof(value));
        close(fd);
        if (received != (ssize_t)sizeof(value)) {
            value = 0u;
        }
    }

    if (value == 0u) {
        value = ((uint64_t)time(NULL) << 32) ^ (uint64_t)(uintptr_t)out;
    }

    (void)snprintf(out, out_size, "%016llx", (unsigned long long)value);
    return 0;
}

static int runtime_ready(void)
{
    return g_runtime.initialized;
}

static int remember_peer(const char *ip, uint16_t port)
{
    peer_entry_t peer;

    if (!runtime_ready() || ip == NULL || ip[0] == '\0' || port == 0u) {
        return 0;
    }
    if (strcmp(ip, g_runtime.origin_ip) == 0 && port == g_runtime.data_port) {
        return 0;
    }

    memset(&peer, 0, sizeof(peer));
    copy_cstr(peer.ip, sizeof(peer.ip), ip);
    peer.data_port = port;
    peer.last_seen_epoch = (uint64_t)time(NULL);
    if (neighbors_add(&g_runtime.neighbors, &peer) != 0 && errno != ENOSPC) {
        return -1;
    }

    return 0;
}

static int send_query_to_peer(const peer_entry_t *peer, const query_msg_t *query)
{
    query_frame_t frame;
    char port[16];
    int fd;
    int rc;

    (void)snprintf(port, sizeof(port), "%u", (unsigned)peer->data_port);
    fd = net_connect(peer->ip, port);
    if (fd < 0) {
        return -1;
    }

    memset(&frame, 0, sizeof(frame));
    encode_header(&frame.header, P2P_MSG_QUERY_FLOOD, (uint32_t)sizeof(frame.payload));
    encode_query(&frame.payload, query);
    rc = net_send_msg(fd, &frame, (uint32_t)sizeof(frame));
    net_close(fd);
    return rc;
}

static int local_search(const char *term, search_results_t *results)
{
    scan_result_t scan;
    size_t i;

    memset(results, 0, sizeof(*results));
    if (scanner_scan_folder(g_runtime.share_folder, &scan) != 0) {
        fprintf(stderr, "distributed search: local scan failed; continuing without local results\n");
        return 0;
    }

    for (i = 0u; i < scan.count && results->count < P2P_MAX_RESULTS; ++i) {
        if (strstr(scan.files[i].name, term) != NULL) {
            results->items[results->count] = scan.files[i];
            copy_cstr(results->items[results->count].owner_ip,
                      sizeof(results->items[results->count].owner_ip),
                      g_runtime.origin_ip);
            results->items[results->count].owner_port = g_runtime.data_port;
            results->count++;
        }
    }

    return 0;
}

static int send_results_to_origin(const query_msg_t *query, const search_results_t *results)
{
    query_result_frame_t frame;
    char port[16];
    size_t i;
    int fd;
    int rc;

    if (results->count == 0u || query->origin_port == 0u || query->origin_ip[0] == '\0') {
        return 0;
    }

    (void)snprintf(port, sizeof(port), "%u", (unsigned)query->origin_port);
    fd = net_connect(query->origin_ip, port);
    if (fd < 0) {
        return -1;
    }

    memset(&frame, 0, sizeof(frame));
    encode_header(&frame.header, P2P_MSG_QUERY_RESULT, (uint32_t)sizeof(frame.payload));
    copy_cstr(frame.payload.query_id, sizeof(frame.payload.query_id), query->query_id);
    frame.payload.result_count = htonl((uint32_t)results->count);
    for (i = 0u; i < results->count; ++i) {
        encode_file_meta(&frame.payload.results[i], &results->items[i]);
    }

    rc = net_send_msg(fd, &frame, (uint32_t)sizeof(frame));
    net_close(fd);
    return rc;
}

void flood_query_cache_reset(void)
{
    if (pthread_mutex_lock(&g_seen_lock) == 0) {
        memset(g_seen, 0, sizeof(g_seen));
        (void)pthread_mutex_unlock(&g_seen_lock);
    }
}

int flood_query_cache_note(const char *query_id, uint64_t now_epoch)
{
    size_t empty_slot = FLOOD_SEEN_CAPACITY;
    size_t oldest_slot = 0u;
    uint64_t oldest_expiry = UINT64_MAX;
    size_t i;

    if (query_id == NULL || query_id[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (pthread_mutex_lock(&g_seen_lock) != 0) {
        return -1;
    }

    for (i = 0u; i < FLOOD_SEEN_CAPACITY; ++i) {
        if (g_seen[i].query_id[0] == '\0') {
            if (empty_slot == FLOOD_SEEN_CAPACITY) {
                empty_slot = i;
            }
            continue;
        }

        if (g_seen[i].expires_at <= now_epoch) {
            memset(&g_seen[i], 0, sizeof(g_seen[i]));
            if (empty_slot == FLOOD_SEEN_CAPACITY) {
                empty_slot = i;
            }
            continue;
        }

        if (strcmp(g_seen[i].query_id, query_id) == 0) {
            (void)pthread_mutex_unlock(&g_seen_lock);
            return 0;
        }

        if (g_seen[i].expires_at < oldest_expiry) {
            oldest_expiry = g_seen[i].expires_at;
            oldest_slot = i;
        }
    }

    if (empty_slot != FLOOD_SEEN_CAPACITY) {
        oldest_slot = empty_slot;
    }

    memset(&g_seen[oldest_slot], 0, sizeof(g_seen[oldest_slot]));
    copy_cstr(g_seen[oldest_slot].query_id,
              sizeof(g_seen[oldest_slot].query_id),
              query_id);
    g_seen[oldest_slot].expires_at = now_epoch + FLOOD_SEEN_TTL_SECONDS;

    (void)pthread_mutex_unlock(&g_seen_lock);
    return 1;
}

uint8_t flood_forward_ttl(uint8_t ttl)
{
    if (ttl == 0u) {
        return 0u;
    }
    if (ttl > P2P_MAX_TTL) {
        ttl = P2P_MAX_TTL;
    }
    return (uint8_t)(ttl - 1u);
}

int flood_listener_start(const flood_config_t *config, neighbor_list_t *neighbors)
{
    if (config == NULL || neighbors == NULL) {
        errno = EINVAL;
        return -1;
    }

    /*
     * Distributed-search frames are received by the unified client data-port
     * listener. This compatibility hook remains for older callers.
     */
    return 0;
}

int flood_forward_query(const query_msg_t *query, const peer_entry_t *sender)
{
    peer_entry_t peers[P2P_MAX_NEIGHBORS];
    size_t peer_count;
    size_t i;

    if (!runtime_ready() || query == NULL) {
        errno = EINVAL;
        return -1;
    }

    peer_count = neighbors_snapshot(&g_runtime.neighbors, peers, P2P_MAX_NEIGHBORS);
    for (i = 0u; i < peer_count; ++i) {
        if (sender != NULL &&
            sender->data_port != 0u &&
            strcmp(peers[i].ip, sender->ip) == 0 &&
            peers[i].data_port == sender->data_port) {
            continue;
        }
        if (strcmp(peers[i].ip, g_runtime.origin_ip) == 0 &&
            peers[i].data_port == g_runtime.data_port) {
            continue;
        }
        (void)send_query_to_peer(&peers[i], query);
    }

    return 0;
}

int flood_handle_query_flood(const char *sender_ip, const query_msg_t *wire_query)
{
    query_msg_t query;
    search_results_t local_results;
    peer_entry_t sender;
    int is_new;

    if (!runtime_ready() || wire_query == NULL) {
        errno = EINVAL;
        return -1;
    }

    decode_query(&query, wire_query);
    is_new = flood_query_cache_note(query.query_id, (uint64_t)time(NULL));
    if (is_new <= 0) {
        return is_new == 0 ? 0 : -1;
    }

    (void)remember_peer(query.origin_ip, query.origin_port);
    if (local_search(query.term, &local_results) == 0) {
        (void)send_results_to_origin(&query, &local_results);
    }

    if (query.ttl > 0u) {
        memset(&sender, 0, sizeof(sender));
        copy_cstr(sender.ip, sizeof(sender.ip), sender_ip);
        sender.data_port = 0u;
        query.ttl = flood_forward_ttl(query.ttl);
        return flood_forward_query(&query, &sender);
    }

    return 0;
}

int flood_handle_query_result(const char *sender_ip, const query_result_t *wire_result)
{
    query_result_t result;
    uint32_t result_count;
    int should_collect;
    size_t i;

    if (!runtime_ready() || wire_result == NULL) {
        errno = EINVAL;
        return -1;
    }

    result = *wire_result;
    result.query_id[P2P_MAX_QUERY_ID - 1u] = '\0';
    result_count = ntohl(result.result_count);
    if (result_count > P2P_MAX_RESULTS) {
        errno = EPROTO;
        return -1;
    }

    if (pthread_mutex_lock(&g_runtime.active_lock) != 0) {
        return -1;
    }
    should_collect = g_runtime.active &&
                     strcmp(g_runtime.active_query_id, result.query_id) == 0;
    (void)pthread_mutex_unlock(&g_runtime.active_lock);

    if (!should_collect) {
        return 0;
    }

    for (i = 0u; i < result_count; ++i) {
        file_meta_t item;

        decode_file_meta(&item, &result.results[i]);
        if (sender_ip != NULL && sender_ip[0] != '\0') {
            copy_cstr(item.owner_ip, sizeof(item.owner_ip), sender_ip);
        }
        (void)aggregator_add(&g_runtime.aggregator, &item);
        (void)remember_peer(item.owner_ip, item.owner_port);
    }

    return 0;
}

static void runtime_destroy_locked(void)
{
    if (g_runtime.initialized) {
        neighbors_destroy(&g_runtime.neighbors);
        aggregator_destroy(&g_runtime.aggregator);
        (void)pthread_mutex_destroy(&g_runtime.active_lock);
        memset(&g_runtime, 0, sizeof(g_runtime));
    }
}

int search_runtime_init(const char *share_folder,
                        const char *server_ip,
                        uint16_t data_port,
                        uint8_t default_ttl,
                        unsigned response_window_ms)
{
    int rc = 0;

    if (share_folder == NULL || data_port == 0u) {
        errno = EINVAL;
        return -1;
    }

    if (pthread_mutex_lock(&g_runtime_guard) != 0) {
        return -1;
    }

    if (g_runtime.initialized) {
        runtime_destroy_locked();
    }

    memset(&g_runtime, 0, sizeof(g_runtime));
    copy_cstr(g_runtime.share_folder, sizeof(g_runtime.share_folder), share_folder);
    detect_origin_ip(server_ip, g_runtime.origin_ip, sizeof(g_runtime.origin_ip));
    g_runtime.data_port = data_port;
    g_runtime.default_ttl = default_ttl > P2P_MAX_TTL ? P2P_MAX_TTL : default_ttl;
    g_runtime.response_window_ms =
        response_window_ms == 0u ? P2P_DEFAULT_SEARCH_TIMEOUT_MS : response_window_ms;

    if (neighbors_init(&g_runtime.neighbors) != 0 ||
        aggregator_init(&g_runtime.aggregator) != 0 ||
        pthread_mutex_init(&g_runtime.active_lock, NULL) != 0) {
        rc = -1;
    } else {
        g_runtime.initialized = 1;
    }

    (void)pthread_mutex_unlock(&g_runtime_guard);
    return rc;
}

void search_runtime_destroy(void)
{
    if (pthread_mutex_lock(&g_runtime_guard) != 0) {
        return;
    }

    runtime_destroy_locked();

    (void)pthread_mutex_unlock(&g_runtime_guard);
}

int search_seed_neighbors(const register_resp_t *response)
{
    size_t i;

    if (!runtime_ready() || response == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (response->neighbor_count > P2P_MAX_NEIGHBORS) {
        errno = EPROTO;
        return -1;
    }

    for (i = 0u; i < response->neighbor_count; ++i) {
        if (neighbors_add(&g_runtime.neighbors, &response->neighbors[i]) != 0 &&
            errno != ENOSPC) {
            return -1;
        }
    }

    return 0;
}

int search_remember_results(const search_results_t *results)
{
    size_t i;

    if (!runtime_ready() || results == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0u; i < results->count; ++i) {
        (void)remember_peer(results->items[i].owner_ip, results->items[i].owner_port);
    }

    return 0;
}

int search_distributed(const char *term, search_results_t *results_out)
{
    query_msg_t query;

    if (!runtime_ready() || term == NULL || results_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(results_out, 0, sizeof(*results_out));
    memset(&query, 0, sizeof(query));
    if (generate_query_id(query.query_id, sizeof(query.query_id)) != 0) {
        return -1;
    }
    copy_cstr(query.term, sizeof(query.term), term);
    copy_cstr(query.origin_ip, sizeof(query.origin_ip), g_runtime.origin_ip);
    query.origin_port = g_runtime.data_port;
    query.ttl = g_runtime.default_ttl;

    if (aggregator_reset(&g_runtime.aggregator) != 0) {
        return -1;
    }
    if (pthread_mutex_lock(&g_runtime.active_lock) != 0) {
        return -1;
    }
    g_runtime.active = 1;
    copy_cstr(g_runtime.active_query_id,
              sizeof(g_runtime.active_query_id),
              query.query_id);
    (void)pthread_mutex_unlock(&g_runtime.active_lock);

    (void)flood_query_cache_note(query.query_id, (uint64_t)time(NULL));
    (void)flood_forward_query(&query, NULL);
    sleep_ms(g_runtime.response_window_ms);

    if (pthread_mutex_lock(&g_runtime.active_lock) == 0) {
        g_runtime.active = 0;
        (void)pthread_mutex_unlock(&g_runtime.active_lock);
    }
    (void)aggregator_collect(&g_runtime.aggregator, results_out);
    return 0;
}
