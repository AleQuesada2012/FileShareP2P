#include "common/hash.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Write the full fixture payload even if write(2) completes partially. */
static void write_all_or_die(int fd, const void *data, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t written_total = 0u;

    while (written_total < len) {
        ssize_t written = write(fd, bytes + written_total, len - written_total);
        assert(written > 0);
        written_total += (size_t)written;
    }
}

/* Create a temporary file containing the exact bytes the hash test needs. */
static void make_temp_file(char path[], const void *data, size_t len)
{
    int fd = mkstemp(path);
    assert(fd >= 0);
    write_all_or_die(fd, data, len);
    assert(close(fd) == 0);
}

/* Verify file hashing agrees with in-memory hashing and is repeatable. */
static void assert_file_hash(const void *payload, size_t payload_len)
{
    char path[] = "/tmp/fs-p2p-hash-XXXXXX";
    uint64_t file_hash;
    uint64_t file_size;

    make_temp_file(path, payload, payload_len);

    assert(hash_file_with_size(path, &file_hash, &file_size) == 0);
    assert(file_size == (uint64_t)payload_len);
    assert(file_hash == hash_bytes(payload, payload_len));
    assert(hash_file(path) == file_hash);
    assert(hash_file(path) == file_hash);

    assert(unlink(path) == 0);
}

/* Missing files must fail cleanly instead of producing stale metadata. */
static void test_missing_path(void)
{
    char path[] = "/tmp/fs-p2p-hash-missing-XXXXXX";
    uint64_t file_hash = UINT64_C(123);
    uint64_t file_size = UINT64_C(456);
    int fd = mkstemp(path);

    assert(fd >= 0);
    assert(close(fd) == 0);
    assert(unlink(path) == 0);

    errno = 0;
    assert(hash_file_with_size(path, &file_hash, &file_size) == -1);
    assert(errno != 0);
    assert(hash_file(path) == 0u);
}

int main(void)
{
    const char payload[] = "hello";
    const unsigned char binary_payload[] = {0x00u, 0xffu, 0x10u, 0x00u, 0x7fu};
    unsigned char large_payload[20000];
    size_t i;

    /* Known-answer checks for FNV-1a constants and a small text payload. */
    assert(hash_bytes("", 0u) == FS_P2P_HASH_OFFSET_BASIS);
    assert(hash_bytes(payload, strlen(payload)) == UINT64_C(0xa430d84680aabd0b));

    /* Invalid input should be reported through errno and a zero hash result. */
    errno = 0;
    assert(hash_bytes(NULL, 1u) == 0u);
    assert(errno == EINVAL);

    /* Build a deterministic payload large enough to cross the file read buffer. */
    for (i = 0u; i < sizeof(large_payload); ++i) {
        large_payload[i] = (unsigned char)(i % 251u);
    }

    /* Exercise empty, text, binary, and multi-buffer file hashing paths. */
    assert_file_hash("", 0u);
    assert_file_hash(payload, strlen(payload));
    assert_file_hash(binary_payload, sizeof(binary_payload));
    assert_file_hash(large_payload, sizeof(large_payload));
    test_missing_path();

    puts("test_hash: ok");
    return 0;
}
