#include "client/repl.h"

#include "client/scanner.h"
#include "client/server_api.h"
#include "transfer/receiver.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "search/neighbors.h"
#include <string.h>

extern neighbor_list_t global_neighbors;

static void print_help(void)
{
    puts("Commands:");
    puts("  find -s <name>       search through central server");
    puts("  find -d <name>       distributed neighbor search");
    puts("  find <name>          server first, then distributed fallback");
    puts("  request <S> <H>      request file by size and hash");
    puts("  help                 show this help");
    puts("  quit                 exit client");
}

static char *skip_spaces(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    return text;
}

static int starts_command(const char *line, const char *command)
{
    size_t len = strlen(command);
    return strncmp(line, command, len) == 0 &&
           (line[len] == '\0' || isspace((unsigned char)line[len]));
}

static void print_find_results(const search_results_t *results)
{
    size_t i;

    if (results == NULL) {
        return;
    }

    printf("Found %zu result(s):\n", results->count);
    for (i = 0u; i < results->count; ++i) {
        const file_meta_t *item = &results->items[i];
        printf("  S=%llu H=%llu %s:%u %s\n",
               (unsigned long long)item->size_bytes,
               (unsigned long long)item->hash,
               item->owner_ip,
               (unsigned)item->owner_port,
               item->name);
    }
}

static int parse_u64_arg(char **cursor, uint64_t *out)
{
    char *start;
    char *end = NULL;
    unsigned long long value;

    if (cursor == NULL || *cursor == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }

    start = skip_spaces(*cursor);
    if (*start == '\0') {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    value = strtoull(start, &end, 0);
    if (errno != 0 || end == start || value > UINT64_MAX) {
        errno = errno == 0 ? ERANGE : errno;
        return -1;
    }

    *out = (uint64_t)value;
    *cursor = end;
    return 0;
}

static int peer_already_added(const peer_entry_t *peers, size_t count, const file_meta_t *item)
{
    size_t i;

    for (i = 0u; i < count; ++i) {
        if (strcmp(peers[i].ip, item->owner_ip) == 0 &&
            peers[i].data_port == item->owner_port) {
            return 1;
        }
    }

    return 0;
}

static size_t collect_cached_peers(const search_results_t *last_results,
                                   uint64_t size,
                                   uint64_t hash,
                                   peer_entry_t *peers,
                                   size_t capacity,
                                   char *source_name,
                                   size_t source_name_size)
{
    size_t count = 0u;
    size_t i;

    if (last_results == NULL || peers == NULL) {
        return 0u;
    }

    for (i = 0u; i < last_results->count && count < capacity; ++i) {
        const file_meta_t *item = &last_results->items[i];
        if (item->size_bytes == size &&
            item->hash == hash &&
            item->owner_port != 0u &&
            !peer_already_added(peers, count, item)) {
            memset(&peers[count], 0, sizeof(peers[count]));
            (void)snprintf(peers[count].ip, sizeof(peers[count].ip), "%s", item->owner_ip);
            peers[count].data_port = item->owner_port;
            if (source_name != NULL && source_name_size > 0u && source_name[0] == '\0') {
                (void)snprintf(source_name, source_name_size, "%s", item->name);
            }
            count++;
        }
    }

    return count;
}

static void remember_results(search_results_t *last_results, const search_results_t *results)
{
    if (last_results != NULL && results != NULL) {
        *last_results = *results;
    }
}

static int run_server_find(const repl_context_t *ctx,
                           const char *term,
                           search_results_t *results)
{
    if (server_find_files(ctx->server_ip, ctx->server_port, term, results) != 0) {
        return -1;
    }

    print_find_results(results);
    return 0;
}

static void refresh_neighbors_from_server(const repl_context_t *ctx)
{
    scan_result_t scan;
    register_resp_t resp;
    uint32_t i;

    if (scanner_scan_folder(ctx->share_folder, &scan) != 0) {
        memset(&scan, 0, sizeof(scan));
    }

    if (server_register_files(ctx->server_ip, ctx->server_port,
                              ctx->data_port, &scan, &resp) != 0) {
        fprintf(stderr, "Warning: could not refresh neighbors from server: %s\n",
                strerror(errno));
        return;
    }

    for (i = 0u; i < resp.neighbor_count; ++i) {
        neighbors_add(&global_neighbors, &resp.neighbors[i]);
    }

    if (resp.neighbor_count > 0u) {
        printf("Refreshed %u neighbor(s) from server.\n", (unsigned)resp.neighbor_count);
    }
}

static int run_distributed_find(const repl_context_t *ctx,
                                const char *term,
                                search_results_t *results)
{

    refresh_neighbors_from_server(ctx);

    printf("Iniciando búsqueda P2P en la red distribuida...\n");
    if (search_distributed(term, ctx->ttl, ctx->search_timeout_ms, results) != 0) {
        return -1;
    }

    print_find_results(results);
    return 0;
}

static int build_identity_find_term(char *buffer,
                                    size_t buffer_size,
                                    uint64_t size,
                                    uint64_t hash)
{
    int written;

    written = snprintf(buffer,
                       buffer_size,
                       "%llu %llu",
                       (unsigned long long)size,
                       (unsigned long long)hash);
    if (written < 0 || written >= (int)buffer_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

static size_t refresh_identity_peers(const repl_context_t *ctx,
                                     uint64_t size,
                                     uint64_t hash,
                                     peer_entry_t *peers,
                                     size_t capacity,
                                     char *source_name,
                                     size_t source_name_size,
                                     search_results_t *last_results)
{
    char identity_term[64];
    search_results_t refreshed;
    size_t count;

    if (build_identity_find_term(identity_term, sizeof(identity_term), size, hash) != 0) {
        return 0u;
    }

    if (server_find_files(ctx->server_ip, ctx->server_port, identity_term, &refreshed) != 0) {
        fprintf(stderr,
                "Warning: identity FIND refresh failed for S=%llu H=%llu: %s\n",
                (unsigned long long)size,
                (unsigned long long)hash,
                strerror(errno));
        return 0u;
    }

    count = collect_cached_peers(&refreshed,
                                 size,
                                 hash,
                                 peers,
                                 capacity,
                                 source_name,
                                 source_name_size);
    printf("Refreshed %zu peer(s) through server identity lookup.\n", count);
    if (count > 0u) {
        remember_results(last_results, &refreshed);
    }

    return count;
}

static void warn_if_share_folder_unavailable(int saved_errno, const char *share_folder)
{
    if (saved_errno == ENOENT || saved_errno == ENOTDIR || saved_errno == EACCES) {
        fprintf(stderr,
                "Warning: share folder is unavailable (%s); continuing.\n",
                share_folder);
    }
}

static void handle_find_command(const repl_context_t *ctx,
                                char *args,
                                search_results_t *last_results)
{
    char *term = skip_spaces(args);
    search_results_t results;

    if (strncmp(term, "-s", 2u) == 0 && (term[2] == '\0' || isspace((unsigned char)term[2]))) {
        term = skip_spaces(term + 2);
        if (*term == '\0') {
            fprintf(stderr, "Usage: find -s <name>\n");
            return;
        }

        if (run_server_find(ctx, term, &results) != 0) {
            perror("server_find_files");
            return;
        }
        remember_results(last_results, &results);
        return;
    }

    if (strncmp(term, "-d", 2u) == 0 && (term[2] == '\0' || isspace((unsigned char)term[2]))) {
        term = skip_spaces(term + 2);
        if (*term == '\0') {
            fprintf(stderr, "Usage: find -d <name>\n");
            return;
        }

        if (run_distributed_find(ctx, term, &results) != 0) {
            perror("search_distributed failed");
            return;
        }

        remember_results(last_results, &results);
        return;
    }


    if (*term == '\0') {
        fprintf(stderr, "Usage: find [-s|-d] <name>\n");
        return;
    }

    if (server_find_files(ctx->server_ip, ctx->server_port, term, &results) == 0) {
        if (results.count > 0u) {
            print_find_results(&results);
            remember_results(last_results, &results);
            return;
        }

        fprintf(stderr,
                "No server results for '%s'; falling back to distributed search.\n",
                term);
    } else {
        fprintf(stderr,
                "Server search unavailable for '%s'; falling back to distributed search: %s\n",
                term,
                strerror(errno));
    }

    if (run_distributed_find(ctx, term, &results) != 0) {
        perror("search_distributed failed");
        return;
    }
    remember_results(last_results, &results);
}

static void handle_request_command(const repl_context_t *ctx,
                                   char *args,
                                   search_results_t *last_results)
{
    peer_entry_t peers[P2P_MAX_RESULTS];
    uint64_t size;
    uint64_t hash;
    size_t peer_count;
    char source_name[P2P_MAX_FILENAME];
    char *cursor = args;

    if (parse_u64_arg(&cursor, &size) != 0 ||
        parse_u64_arg(&cursor, &hash) != 0 ||
        *skip_spaces(cursor) != '\0') {
        fprintf(stderr, "Usage: request <S> <H>\n");
        return;
    }

    memset(source_name, 0, sizeof(source_name));
    peer_count = refresh_identity_peers(ctx,
                                        size,
                                        hash,
                                        peers,
                                        P2P_MAX_RESULTS,
                                        source_name,
                                        sizeof(source_name),
                                        last_results);
    if (peer_count == 0u) {
        peer_count = collect_cached_peers(last_results,
                                          size,
                                          hash,
                                          peers,
                                          P2P_MAX_RESULTS,
                                          source_name,
                                          sizeof(source_name));
    }
    if (peer_count == 0u) {
        fprintf(stderr, "No peers found for S=%llu H=%llu.\n",
                (unsigned long long)size,
                (unsigned long long)hash);
        return;
    }

    if (transfer_request(hash, size, peers, peer_count, source_name, ctx->share_folder) != 0) {
        int saved_errno = errno;
        errno = saved_errno;
        perror("transfer_request");
        warn_if_share_folder_unavailable(saved_errno, ctx->share_folder);
        return;
    }
}

int repl_run(const repl_context_t *ctx)
{
    char line[512];
    search_results_t last_results;

    if (ctx == NULL) {
        return -1;
    }
    memset(&last_results, 0, sizeof(last_results));

    printf("Client ready: server=%s:%s data_port=%u ttl=%u timeout=%ums\n",
           ctx->server_ip,
           ctx->server_port,
           (unsigned)ctx->data_port,
           (unsigned)ctx->ttl,
           ctx->search_timeout_ms);
    print_help();

    for (;;) {
        char *newline;

        printf("p2p> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            putchar('\n');
            return 0;
        }

        newline = strchr(line, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            return 0;
        }
        if (strcmp(line, "help") == 0 || line[0] == '\0') {
            print_help();
            continue;
        }
        if (starts_command(line, "find")) {
            handle_find_command(ctx, skip_spaces(line + 4), &last_results);
            continue;
        }
        if (starts_command(line, "request")) {
            handle_request_command(ctx, skip_spaces(line + 7), &last_results);
            continue;
        }

        fprintf(stderr, "Unknown command. Type 'help' for options.\n");
    }
}
