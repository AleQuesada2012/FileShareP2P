#include "transfer/sender.h"

#include <errno.h>

int transfer_send_range(int client_fd, const char *path, const transfer_req_t *request)
{
    (void)client_fd;
    (void)path;
    (void)request;
    errno = ENOSYS;
    return -1;
}
