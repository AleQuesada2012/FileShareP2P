#include "client/repl.h"

#include "client/server_api.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void print_help(void)
{
    puts("Commands:");
    puts("  find -s <name>       search through central server");
    puts("  find -d <name>       distributed neighbor search (TODO)");
    puts("  find <name>          server first, then distributed fallback (TODO)");
    puts("  request <S> <H>      request file by size and hash (TODO)");
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

static void handle_find_command(const repl_context_t *ctx, char *args)
{
    char *term = skip_spaces(args);
    search_results_t results;

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
        return;
    }

    if (strncmp(term, "-d", 2u) == 0 && (term[2] == '\0' || isspace((unsigned char)term[2]))) {
        puts("TODO: distributed search is not implemented yet.");
        return;
    }

    if (*term == '\0') {
        fprintf(stderr, "Usage: find [-s|-d] <name>\n");
        return;
    }

    puts("TODO: server-first search fallback is not implemented yet. Use: find -s <name>");
}

int repl_run(const repl_context_t *ctx)
{
    char line[512];

    if (ctx == NULL) {
        return -1;
    }

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
            handle_find_command(ctx, skip_spaces(line + 4));
            continue;
        }
        if (starts_command(line, "request")) {
            printf("TODO: dispatch transfer command: %s\n", line);
            continue;
        }

        fprintf(stderr, "Unknown command. Type 'help' for options.\n");
    }
}
