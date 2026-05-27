#include "search/flood.h"

#include <errno.h>

int flood_listener_start(const flood_config_t *config, neighbor_list_t *neighbors)
{
    (void)config;
    (void)neighbors;
    errno = ENOSYS;
    return -1;
}

int flood_forward_query(const query_msg_t *query, const peer_entry_t *sender)
{
    (void)query;
    (void)sender;
    errno = ENOSYS;
    return -1;
}
