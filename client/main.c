#include "client/repl.h"
#include "client/scanner.h"
#include "client/server_api.h"
#include "common/net.h"
#include "common/protocol.h"
#include "search/neighbors.h"
#include "transfer/listener.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s <server_ip> <server_port> <data_port> <share_folder> "
            "[--ttl <n>] [--search-timeout <ms>]\n",
            program);
}

static int parse_uint16(const char *text, uint16_t *out)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > 65535ul) {
        return -1;
    }

    *out = (uint16_t)value;
    return 0;
}

static int parse_uint8_limited(const char *text, uint8_t max_value, uint8_t *out)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > (unsigned long)max_value) {
        return -1;
    }

    *out = (uint8_t)value;
    return 0;
}

static int parse_unsigned(const char *text, unsigned *out)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT_MAX) {
        return -1;
    }

    *out = (unsigned)value;
    return 0;
}

int main(int argc, char **argv)
{
    repl_context_t ctx;
    register_resp_t register_response;
    scan_result_t scan;
    int repl_rc;
    int i;

    if (argc < 5) {
        usage(argv[0]);
        return 2;
    }

    memset(&ctx, 0, sizeof(ctx));
    (void)snprintf(ctx.server_ip, sizeof(ctx.server_ip), "%s", argv[1]);
    (void)snprintf(ctx.server_port, sizeof(ctx.server_port), "%s", argv[2]);
    if (parse_uint16(argv[3], &ctx.data_port) != 0) {
        fprintf(stderr, "Invalid data_port: %s\n", argv[3]);
        return 2;
    }
    (void)snprintf(ctx.share_folder, sizeof(ctx.share_folder), "%s", argv[4]);
    ctx.ttl = P2P_DEFAULT_TTL;
    ctx.search_timeout_ms = P2P_DEFAULT_SEARCH_TIMEOUT_MS;

    for (i = 5; i < argc; ++i) {
        if (strcmp(argv[i], "--ttl") == 0 && i + 1 < argc) {
            if (parse_uint8_limited(argv[++i], P2P_MAX_TTL, &ctx.ttl) != 0) {
                fprintf(stderr, "Invalid --ttl value, expected 0..%u\n", (unsigned)P2P_MAX_TTL);
                return 2;
            }
        } else if (strcmp(argv[i], "--search-timeout") == 0 && i + 1 < argc) {
            if (parse_unsigned(argv[++i], &ctx.search_timeout_ms) != 0) {
                fprintf(stderr, "Invalid --search-timeout value\n");
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (net_ignore_sigpipe() != 0) {
        perror("net_ignore_sigpipe");
        return 1;
    }

    if (scanner_scan_folder(ctx.share_folder, &scan) != 0) {
        fprintf(stderr, "Warning: initial share-folder scan failed; continuing for hot-unplug tolerance.\n");
        memset(&scan, 0, sizeof(scan));
    }
    scanner_print_result(&scan);

    if (search_runtime_init(ctx.share_folder,
                            ctx.server_ip,
                            ctx.data_port,
                            ctx.ttl,
                            ctx.search_timeout_ms) != 0) {
        perror("search_runtime_init");
        fprintf(stderr, "Warning: distributed search is disabled.\n");
    }

    if (server_register_files(ctx.server_ip, ctx.server_port, ctx.data_port, &scan, &register_response) != 0) {
        perror("server_register_files");
        fprintf(stderr, "Warning: registration failed; continuing with local REPL.\n");
    } else {
        printf("Registered %zu file(s) with server %s:%s; received %u neighbor(s).\n",
               scan.count,
               ctx.server_ip,
               ctx.server_port,
               (unsigned)register_response.neighbor_count);
        if (search_seed_neighbors(&register_response) != 0) {
            perror("search_seed_neighbors");
            fprintf(stderr, "Warning: server neighbor seeding failed.\n");
        }
    }

    if (transfer_listener_start(argv[3], ctx.share_folder) != 0) {
        perror("transfer_listener_start");
        fprintf(stderr, "Warning: incoming peer messages are disabled.\n");
    }

    repl_rc = repl_run(&ctx);
    search_runtime_destroy();
    return repl_rc == 0 ? 0 : 1;
}
