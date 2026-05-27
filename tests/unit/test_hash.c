#include "common/hash.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char payload[] = "hello";
    char path[] = "/tmp/fs-p2p-hash-XXXXXX";
    uint64_t file_hash;
    uint64_t file_size;
    int fd;

    assert(hash_bytes("", 0u) == FS_P2P_HASH_OFFSET_BASIS);
    assert(hash_bytes(payload, strlen(payload)) == UINT64_C(0xa430d84680aabd0b));

    fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, payload, strlen(payload)) == (ssize_t)strlen(payload));
    assert(close(fd) == 0);

    assert(hash_file_with_size(path, &file_hash, &file_size) == 0);
    assert(file_size == (uint64_t)strlen(payload));
    assert(file_hash == hash_bytes(payload, strlen(payload)));
    assert(hash_file(path) == file_hash);

    assert(unlink(path) == 0);
    puts("test_hash: ok");
    return 0;
}
