/* Test shim for ppathstatus.h */
#ifndef _PSYNC_PATHSTATUS_H
#define _PSYNC_PATHSTATUS_H
#include <stdint.h>
void psync_path_status_reload_syncs(void);
void psync_restat_sync_folders_add(uint32_t syncid, const char *path);
#endif
