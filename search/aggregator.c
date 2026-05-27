#include "search/aggregator.h"

#include <errno.h>
#include <string.h>

int aggregator_init(response_aggregator_t *aggregator)
{
    if (aggregator == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(aggregator, 0, sizeof(*aggregator));
    return pthread_mutex_init(&aggregator->lock, NULL);
}

void aggregator_destroy(response_aggregator_t *aggregator)
{
    if (aggregator != NULL) {
        (void)pthread_mutex_destroy(&aggregator->lock);
    }
}

int aggregator_add(response_aggregator_t *aggregator, const file_meta_t *result)
{
    if (aggregator == NULL || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (pthread_mutex_lock(&aggregator->lock) != 0) {
        return -1;
    }

    if (aggregator->results.count >= P2P_MAX_RESULTS) {
        (void)pthread_mutex_unlock(&aggregator->lock);
        errno = ENOSPC;
        return -1;
    }

    aggregator->results.items[aggregator->results.count++] = *result;
    return pthread_mutex_unlock(&aggregator->lock);
}

size_t aggregator_collect(response_aggregator_t *aggregator, search_results_t *out)
{
    if (aggregator == NULL || out == NULL) {
        errno = EINVAL;
        return 0u;
    }

    if (pthread_mutex_lock(&aggregator->lock) != 0) {
        return 0u;
    }

    *out = aggregator->results;
    (void)pthread_mutex_unlock(&aggregator->lock);
    return out->count;
}
