#ifndef FS_P2P_TRANSFER_SENDER_H
#define FS_P2P_TRANSFER_SENDER_H

#include "common/protocol.h"

int transfer_send_range(int client_fd, const char *path, const transfer_req_t *request);

#endif
