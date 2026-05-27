#include "common/hash.h"

#include <errno.h>
#include <stdio.h>

uint64_t hash_bytes(const void *data, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)data;
    uint64_t hash = FS_P2P_HASH_OFFSET_BASIS;
    size_t i;

    if (data == NULL && len > 0u) {
        errno = EINVAL;
        return 0u;
    }

    for (i = 0u; i < len; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= FS_P2P_HASH_PRIME;
    }

    return hash;
}

uint64_t hash_file(const char *path)
{
    uint64_t hash = 0u;

    if (hash_file_with_size(path, &hash, NULL) != 0) {
        return 0u;
    }

    return hash;
}

int hash_file_with_size(const char *path, uint64_t *hash_out, uint64_t *size_out)
{
    unsigned char buffer[8192];
    FILE *file;
    uint64_t hash = FS_P2P_HASH_OFFSET_BASIS;
    uint64_t total = 0u;

    if (path == NULL || hash_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    for (;;) {
        size_t nread = fread(buffer, 1u, sizeof(buffer), file);
        size_t i;

        for (i = 0u; i < nread; ++i) {
            hash ^= (uint64_t)buffer[i];
            hash *= FS_P2P_HASH_PRIME;
        }

        if (UINT64_MAX - total < (uint64_t)nread) {
            fclose(file);
            errno = EOVERFLOW;
            return -1;
        }
        total += (uint64_t)nread;

        if (nread < sizeof(buffer)) {
            if (ferror(file)) {
                fclose(file);
                return -1;
            }
            break;
        }
    }

    if (fclose(file) != 0) {
        return -1;
    }

    *hash_out = hash;
    if (size_out != NULL) {
        *size_out = total;
    }

    return 0;
}
