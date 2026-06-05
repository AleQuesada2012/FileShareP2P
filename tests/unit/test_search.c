#include "search/aggregator.h"
#include "search/flood.h"
#include "search/neighbors.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static peer_entry_t make_peer(const char *ip, uint16_t port)
{
    peer_entry_t peer;

    memset(&peer, 0, sizeof(peer));
    (void)snprintf(peer.ip, sizeof(peer.ip), "%s", ip);
    peer.data_port = port;
    return peer;
}

static file_meta_t make_result(const char *name, uint64_t hash, uint64_t size)
{
    file_meta_t result;

    memset(&result, 0, sizeof(result));
    (void)snprintf(result.name, sizeof(result.name), "%s", name);
    result.hash = hash;
    result.size_bytes = size;
    (void)snprintf(result.owner_ip, sizeof(result.owner_ip), "%s", "127.0.0.1");
    result.owner_port = 23001u;
    return result;
}

static void test_neighbor_deduplication(void)
{
    neighbor_list_t neighbors;
    peer_entry_t snapshot[P2P_MAX_NEIGHBORS];
    peer_entry_t first = make_peer("127.0.0.1", 21001u);
    peer_entry_t duplicate = make_peer("127.0.0.1", 21001u);
    peer_entry_t second = make_peer("127.0.0.1", 21002u);

    assert(neighbors_init(&neighbors) == 0);
    assert(neighbors_add(&neighbors, &first) == 0);
    assert(neighbors_add(&neighbors, &duplicate) == 0);
    assert(neighbors_add(&neighbors, &second) == 0);
    assert(neighbors_snapshot(&neighbors, snapshot, P2P_MAX_NEIGHBORS) == 2u);
    assert(snapshot[0].data_port == 21001u);
    assert(snapshot[1].data_port == 21002u);
    neighbors_destroy(&neighbors);
}

static void test_query_id_deduplication(void)
{
    flood_query_cache_reset();
    assert(flood_query_cache_note("0123456789abcdef", 100u) == 1);
    assert(flood_query_cache_note("0123456789abcdef", 120u) == 0);
    assert(flood_query_cache_note("0123456789abcdef", 161u) == 1);
}

static void test_ttl_behavior(void)
{
    assert(flood_forward_ttl(0u) == 0u);
    assert(flood_forward_ttl(1u) == 0u);
    assert(flood_forward_ttl(4u) == 3u);
    assert(flood_forward_ttl(99u) == P2P_MAX_TTL - 1u);
}

static void test_aggregation_results(void)
{
    response_aggregator_t aggregator;
    search_results_t out;
    file_meta_t first = make_result("alpha.txt", 1u, 10u);
    file_meta_t second = make_result("beta.txt", 2u, 20u);

    assert(aggregator_init(&aggregator) == 0);
    assert(aggregator_add(&aggregator, &first) == 0);
    assert(aggregator_add(&aggregator, &first) == 0);
    assert(aggregator_add(&aggregator, &second) == 0);
    assert(aggregator_collect(&aggregator, &out) == 2u);
    assert(out.count == 2u);
    assert(strcmp(out.items[0].name, "alpha.txt") == 0);
    assert(strcmp(out.items[1].name, "beta.txt") == 0);
    assert(aggregator_reset(&aggregator) == 0);
    assert(aggregator_collect(&aggregator, &out) == 0u);
    aggregator_destroy(&aggregator);
}

static void test_empty_distributed_search_window(void)
{
    search_results_t results;

    assert(search_runtime_init("/tmp", "127.0.0.1", 24001u, P2P_DEFAULT_TTL, 1u) == 0);
    assert(search_distributed("no-neighbors-needed", &results) == 0);
    assert(results.count == 0u);
    search_runtime_destroy();
}

int main(void)
{
    test_neighbor_deduplication();
    test_query_id_deduplication();
    test_ttl_behavior();
    test_aggregation_results();
    test_empty_distributed_search_window();

    puts("test_search: ok");
    return 0;
}
