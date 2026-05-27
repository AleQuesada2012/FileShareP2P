#include "server/query_handler.h"

#include "common/net.h"

#include <stdio.h>

int query_server_run(const char *port, registry_t *registry)
{
    int listen_fd;

    (void)registry;

    listen_fd = net_listen(port, 16);
    if (listen_fd < 0) {
        perror("net_listen");
        return -1;
    }

    printf("p2p-server listening on port %s\n", port);
    printf("TODO: accept REGISTER/FIND messages and dispatch worker threads.\n");

    net_close(listen_fd);
    return 0;
}
