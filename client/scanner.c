#include "client/scanner.h"

#include "common/hash.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static int add_file(const char *path, scan_result_t *result)
{
    file_meta_t *meta;

    if (result->count >= SCANNER_MAX_FILES) {
        fprintf(stderr, "scanner: file limit reached, skipping %s\n", path);
        return 0;
    }

    meta = &result->files[result->count];
    memset(meta, 0, sizeof(*meta));
    (void)snprintf(meta->name, sizeof(meta->name), "%s", base_name(path));

    if (hash_file_with_size(path, &meta->hash, &meta->size_bytes) != 0) {
        perror("hash_file_with_size");
        return -1;
    }

    result->count++;
    return 0;
}

static int scan_recursive(const char *folder, scan_result_t *result)
{
    DIR *dir;
    struct dirent *entry;

    dir = opendir(folder);
    if (dir == NULL) {
        perror("opendir");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name) >= (int)sizeof(path)) {
            fprintf(stderr, "scanner: path too long, skipping %s/%s\n", folder, entry->d_name);
            continue;
        }

        if (lstat(path, &st) != 0) {
            perror("lstat");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (scan_recursive(path, result) != 0) {
                (void)closedir(dir);
                return -1;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (add_file(path, result) != 0) {
                (void)closedir(dir);
                return -1;
            }
        }
    }

    if (closedir(dir) != 0) {
        perror("closedir");
        return -1;
    }

    return 0;
}

int scanner_scan_folder(const char *folder, scan_result_t *result)
{
    if (folder == NULL || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(result, 0, sizeof(*result));
    if (access(folder, R_OK) != 0) {
        perror("access");
        return -1;
    }

    return scan_recursive(folder, result);
}

void scanner_print_result(const scan_result_t *result)
{
    size_t i;

    if (result == NULL) {
        return;
    }

    printf("Found %zu shared file(s):\n", result->count);
    for (i = 0u; i < result->count; ++i) {
        printf("  %s size=%llu hash=%llu\n",
               result->files[i].name,
               (unsigned long long)result->files[i].size_bytes,
               (unsigned long long)result->files[i].hash);
    }
}
