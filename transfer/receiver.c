#include "transfer/receiver.h"

#include <errno.h>

int transfer_request(uint64_t hash,
                     uint64_t size,
                     const peer_entry_t *peers,
                     size_t peer_count,
                     const char *destination_folder)
{
    (void)hash;
    (void)size;
    (void)peers;
    (void)peer_count;
    (void)destination_folder;
    errno = ENOSYS;
    return -1;
}
