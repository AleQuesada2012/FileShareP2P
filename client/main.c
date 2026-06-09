#include "client/repl.h"
#include "client/scanner.h"
#include "client/server_api.h"
#include "common/net.h"
#include "common/protocol.h"
#include "transfer/listener.h"

#include "search/flood.h"
#include "search/neighbors.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

flood_config_t global_flood_config;
extern neighbor_list_t global_neighbors;

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
    if (errno != 0 || end == text || *end != '\0' || value > 65535ul) return -1;
    *out = (uint16_t)value;
    return 0;
}

static int parse_uint8_limited(const char *text, uint8_t max_value, uint8_t *out)
{
    char *end = NULL;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > (unsigned long)max_value) return -1;
    *out = (uint8_t)value;
    return 0;
}

static int parse_unsigned(const char *text, unsigned *out)
{
    char *end = NULL;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT_MAX) return -1;
    *out = (unsigned)value;
    return 0;
}

int main(int argc, char **argv)
{
    repl_context_t ctx;
    register_resp_t register_response;
    scan_result_t scan;
    int i;

    neighbors_init(&global_neighbors);

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
            if (parse_uint8_limited(argv[++i], P2P_MAX_TTL, &ctx.ttl) != 0) return 2;
        } else if (strcmp(argv[i], "--search-timeout") == 0 && i + 1 < argc) {
            if (parse_unsigned(argv[++i], &ctx.search_timeout_ms) != 0) return 2;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (net_ignore_sigpipe() != 0) return 1;

    if (transfer_listener_start(argv[3], ctx.share_folder) != 0) {
        fprintf(stderr, "Warning: incoming file transfers are disabled.\n");
    }

    if (scanner_scan_folder(ctx.share_folder, &scan) != 0) {
        memset(&scan, 0, sizeof(scan));
    }
    scanner_print_result(&scan);

    if (server_register_files(ctx.server_ip, ctx.server_port, ctx.data_port, &scan, &register_response) != 0) {
        fprintf(stderr, "Warning: registration failed; continuing with local REPL.\n");
    } else {
        printf("Registered %zu file(s) with server %s:%s; received %u neighbor(s).\n",
               scan.count, ctx.server_ip, ctx.server_port, (unsigned)register_response.neighbor_count);


        for (uint32_t j = 0; j < register_response.neighbor_count; j++) {
            neighbors_add(&global_neighbors, &register_response.neighbors[j]);
        }
    }

    memset(&global_flood_config, 0, sizeof(global_flood_config));
    global_flood_config.default_ttl = ctx.ttl;
    global_flood_config.response_window_ms = ctx.search_timeout_ms;
    global_flood_config.listen_port = ctx.data_port + 100; // Desplazamos tu puerto +100
    global_flood_config.data_port = ctx.data_port;
    strncpy(global_flood_config.node_ip, "127.0.0.1", P2P_MAX_IP_LEN - 1);
    strncpy(global_flood_config.share_folder, ctx.share_folder, sizeof(global_flood_config.share_folder) - 1);

    if (flood_listener_start(&global_flood_config, &global_neighbors) != 0) {
        fprintf(stderr, "Warning: flood listener failed to start.\n");
    }

    return repl_run(&ctx) == 0 ? 0 : 1;
}