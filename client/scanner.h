#ifndef FS_P2P_CLIENT_SCANNER_H
#define FS_P2P_CLIENT_SCANNER_H

#include "common/protocol.h"

#include <stddef.h>

#define SCANNER_MAX_FILES P2P_MAX_FILES_PER_PEER

typedef struct {
    size_t count;
    file_meta_t files[SCANNER_MAX_FILES];
} scan_result_t;

int scanner_scan_folder(const char *folder, scan_result_t *result);
void scanner_print_result(const scan_result_t *result);

#endif
