#ifndef FS_P2P_SERVER_QUERY_HANDLER_H
#define FS_P2P_SERVER_QUERY_HANDLER_H

#include "server/registry.h"

int query_server_run(const char *port, registry_t *registry);
int query_handle_client(int client_fd, const char *client_ip, registry_t *registry);

#endif
