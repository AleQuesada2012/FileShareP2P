#include "server/registry.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static file_meta_t make_file(const char *name, uint64_t hash, uint64_t size)
{
    file_meta_t file;

    memset(&file, 0, sizeof(file));
    (void)snprintf(file.name, sizeof(file.name), "%s", name);
    file.hash = hash;
    file.size_bytes = size;
    return file;
}

int main(void)
{
    registry_t registry;
    file_meta_t first_files[2];
    file_meta_t second_files[1];
    file_meta_t results[4];
    peer_entry_t peers[4];
    size_t count;

    assert(registry_init(&registry) == 0);

    first_files[0] = make_file("movie.mp4", UINT64_C(1001), UINT64_C(4096));
    first_files[1] = make_file("notes.txt", UINT64_C(1002), UINT64_C(128));
    assert(registry_register_peer(&registry, "127.0.0.1", 7001u, first_files, 2u) == 0);

    second_files[0] = make_file("movie-copy.mp4", UINT64_C(1001), UINT64_C(4096));
    assert(registry_register_peer(&registry, "127.0.0.2", 7002u, second_files, 1u) == 0);

    memset(results, 0, sizeof(results));
    count = registry_find_by_name(&registry, "movie", results, 4u);
    assert(count == 2u);
    assert(results[0].owner_port == 7001u);
    assert(results[1].owner_port == 7002u);

    memset(results, 0, sizeof(results));
    count = registry_find_by_identity(&registry, UINT64_C(1001), UINT64_C(4096), results, 4u);
    assert(count == 2u);

    memset(peers, 0, sizeof(peers));
    count = registry_recent_peers(&registry, peers, 4u);
    assert(count == 2u);
    assert(strcmp(peers[0].ip, "127.0.0.2") == 0);
    assert(peers[0].data_port == 7002u);
    assert(strcmp(peers[1].ip, "127.0.0.1") == 0);

    memset(peers, 0, sizeof(peers));
    count = registry_recent_peers_except(&registry, "127.0.0.2", 7002u, peers, 4u);
    assert(count == 1u);
    assert(strcmp(peers[0].ip, "127.0.0.1") == 0);

    assert(registry_register_peer(&registry, "127.0.0.2", 7002u, second_files, 1u) == 0);
    memset(peers, 0, sizeof(peers));
    count = registry_recent_peers_except(&registry, "127.0.0.2", 7002u, peers, 4u);
    assert(count == 1u);
    assert(strcmp(peers[0].ip, "127.0.0.1") == 0);

    registry_destroy(&registry);

    puts("test_registry: ok");
    return 0;
}
