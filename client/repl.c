#include "client/repl.h"

#include "client/server_api.h"
#include "transfer/receiver.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
                                   size_t capacity)
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
            count++;
        }
    }

    return count;
}

static void handle_find_command(const repl_context_t *ctx,
                                char *args,
                                search_results_t *last_results)
{
    char *term = skip_spaces(args);
    search_results_t results;
    int server_rc;

    if (strncmp(term, "-s", 2u) == 0 && (term[2] == '\0' || isspace((unsigned char)term[2]))) {
        term = skip_spaces(term + 2);
        if (*term == '\0') {
            fprintf(stderr, "Usage: find -s <name>\n");
            return;
        }

        if (server_find_files(ctx->server_ip, ctx->server_port, term, &results) != 0) {
            perror("server_find_files");
            return;
        }
        print_find_results(&results);
        (void)search_remember_results(&results);
        if (last_results != NULL) {
            *last_results = results;
        }
        return;
    }

    if (strncmp(term, "-d", 2u) == 0 && (term[2] == '\0' || isspace((unsigned char)term[2]))) {
        term = skip_spaces(term + 2);
        if (*term == '\0') {
            fprintf(stderr, "Usage: find -d <name>\n");
            return;
        }

        if (search_distributed(term, &results) != 0) {
            perror("search_distributed");
            return;
        }
        print_find_results(&results);
        (void)search_remember_results(&results);
        if (last_results != NULL) {
            *last_results = results;
        }
        return;
    }

    if (*term == '\0') {
        fprintf(stderr, "Usage: find [-s|-d] <name>\n");
        return;
    }

    server_rc = server_find_files(ctx->server_ip, ctx->server_port, term, &results);
    if (server_rc == 0 && results.count > 0u) {
        print_find_results(&results);
        (void)search_remember_results(&results);
        if (last_results != NULL) {
            *last_results = results;
        }
        return;
    }
    if (server_rc != 0) {
        perror("server_find_files");
        fprintf(stderr, "Falling back to distributed search.\n");
    }

    if (search_distributed(term, &results) != 0) {
        perror("search_distributed");
        return;
    }
    print_find_results(&results);
    (void)search_remember_results(&results);
    if (last_results != NULL) {
        *last_results = results;
    }
}

static void handle_request_command(const repl_context_t *ctx,
                                   char *args,
                                   const search_results_t *last_results)
{
    peer_entry_t peers[P2P_MAX_RESULTS];
    uint64_t size;
    uint64_t hash;
    size_t peer_count;
    char *cursor = args;

    if (parse_u64_arg(&cursor, &size) != 0 ||
        parse_u64_arg(&cursor, &hash) != 0 ||
        *skip_spaces(cursor) != '\0') {
        fprintf(stderr, "Usage: request <S> <H>\n");
        return;
    }

    peer_count = collect_cached_peers(last_results, size, hash, peers, P2P_MAX_RESULTS);
    if (peer_count == 0u) {
        fprintf(stderr, "No cached peers for S=%llu H=%llu. Run find -s <name> first.\n",
                (unsigned long long)size,
                (unsigned long long)hash);
        return;
    }

    if (transfer_request(hash, size, peers, peer_count, ctx->share_folder) != 0) {
        perror("transfer_request");
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
