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

int aggregator_reset(response_aggregator_t *aggregator)
{
    if (aggregator == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (pthread_mutex_lock(&aggregator->lock) != 0) {
        return -1;
    }

    memset(&aggregator->results, 0, sizeof(aggregator->results));
    return pthread_mutex_unlock(&aggregator->lock);
}

static int same_result(const file_meta_t *a, const file_meta_t *b)
{
    return a->hash == b->hash &&
           a->size_bytes == b->size_bytes &&
           a->owner_port == b->owner_port &&
           strcmp(a->owner_ip, b->owner_ip) == 0 &&
           strcmp(a->name, b->name) == 0;
}

int aggregator_add(response_aggregator_t *aggregator, const file_meta_t *result)
{
    size_t i;

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

    for (i = 0u; i < aggregator->results.count; ++i) {
        if (same_result(&aggregator->results.items[i], result)) {
            return pthread_mutex_unlock(&aggregator->lock);
        }
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
