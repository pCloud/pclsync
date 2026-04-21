/* Test shim for ptasks.h */
#ifndef _PSYNC_TASKS_H
#define _PSYNC_TASKS_H
#include <stdint.h>
void psync_task_create_local_folder(uint32_t syncid, uint64_t folderid, uint64_t lfolderid);
void psync_task_download_file_silent(uint32_t syncid, uint64_t fileid, uint64_t lfolderid, const char *name);
#endif
