#include "client/repl.h"

#include <stdio.h>
#include <string.h>

static void print_help(void)
{
    puts("Commands:");
    puts("  find -s <name>       search through central server (TODO)");
    puts("  find -d <name>       distributed neighbor search (TODO)");
    puts("  find <name>          server first, then distributed fallback (TODO)");
    puts("  request <S> <H>      request file by size and hash (TODO)");
    puts("  help                 show this help");
    puts("  quit                 exit client");
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
        if (strncmp(line, "find", 4u) == 0) {
            printf("TODO: dispatch search command: %s\n", line);
            continue;
        }
        if (strncmp(line, "request", 7u) == 0) {
            printf("TODO: dispatch transfer command: %s\n", line);
            continue;
        }

        fprintf(stderr, "Unknown command. Type 'help' for options.\n");
    }
}
