/* Test shim for pfolder.h */
#ifndef _PSYNC_FOLDER_H
#define _PSYNC_FOLDER_H
#include <stdint.h>
#define PSYNC_INVALID_FOLDERID ((uint64_t)-1)
uint64_t psync_get_folderid_by_path_or_create(const char *path);
#endif
