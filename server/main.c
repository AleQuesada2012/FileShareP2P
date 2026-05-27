#include "common/net.h"
#include "server/query_handler.h"
#include "server/registry.h"

#include <stdio.h>

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s <listen_port>\n", program);
}

int main(int argc, char **argv)
{
    registry_t registry;
    int rc;

    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }

    if (net_ignore_sigpipe() != 0) {
        perror("net_ignore_sigpipe");
        return 1;
    }

    if (registry_init(&registry) != 0) {
        perror("registry_init");
        return 1;
    }

    rc = query_server_run(argv[1], &registry);
    registry_destroy(&registry);
    return rc == 0 ? 0 : 1;
}
