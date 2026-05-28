#ifndef FS_P2P_CLIENT_SERVER_API_H
#define FS_P2P_CLIENT_SERVER_API_H

#include "client/scanner.h"
#include "common/protocol.h"
#include "search/neighbors.h"

#include <stdint.h>

int server_register_files(const char *server_ip,
                          const char *server_port,
                          uint16_t data_port,
                          const scan_result_t *scan,
                          register_resp_t *response_out);

int server_find_files(const char *server_ip,
                      const char *server_port,
                      const char *term,
                      search_results_t *results_out);

#endif
