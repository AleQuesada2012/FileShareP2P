#ifndef FS_P2P_CLIENT_REPL_H
#define FS_P2P_CLIENT_REPL_H

#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char server_ip[46];
    char server_port[16];
    uint16_t data_port;
    char share_folder[PATH_MAX];
    uint8_t ttl;
    unsigned search_timeout_ms;
} repl_context_t;

int repl_run(const repl_context_t *ctx);

#endif
