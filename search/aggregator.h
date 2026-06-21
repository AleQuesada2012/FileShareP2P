#ifndef FS_P2P_SEARCH_AGGREGATOR_H
#define FS_P2P_SEARCH_AGGREGATOR_H

#include "search/neighbors.h"

#include <pthread.h>

typedef struct {
    pthread_mutex_t lock;
    search_results_t results;
} response_aggregator_t;

int aggregator_init(response_aggregator_t *aggregator);
int aggregator_clear(response_aggregator_t *aggregator);
void aggregator_destroy(response_aggregator_t *aggregator);
int aggregator_add(response_aggregator_t *aggregator, const file_meta_t *result);
size_t aggregator_collect(response_aggregator_t *aggregator, search_results_t *out);

#endif
