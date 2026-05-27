#ifndef FS_P2P_COMMON_HASH_H
#define FS_P2P_COMMON_HASH_H

#include <stddef.h>
#include <stdint.h>

#define FS_P2P_HASH_OFFSET_BASIS UINT64_C(14695981039346656037)
#define FS_P2P_HASH_PRIME UINT64_C(1099511628211)

uint64_t hash_bytes(const void *data, size_t len);
uint64_t hash_file(const char *path);
int hash_file_with_size(const char *path, uint64_t *hash_out, uint64_t *size_out);

#endif
