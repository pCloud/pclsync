/* Test shim for psynclib.h — provides malloc/free wrappers and basic types */
#ifndef _PSYNC_LIB_H
#define _PSYNC_LIB_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t psync_folderid_t;
typedef uint64_t psync_fileid_t;
typedef uint64_t psync_fileorfolderid_t;
typedef uint32_t psync_syncid_t;
typedef uint32_t psync_synctype_t;
typedef uint32_t psync_eventtype_t;

#define PSYNC_DOWNLOAD_ONLY  1
#define PSYNC_UPLOAD_ONLY    2
#define PSYNC_FULL           3
#define PSYNC_PERM_READ      1
#define PERROR_OFFLINE       11
#define PSYNC_INVALID_FOLDERID ((psync_folderid_t)-1)

int psync_is_name_to_ignore(const char *name);
extern uint32_t psync_error;

/* psync_free must be a real function (not a macro) so its address can be taken
 * (e.g., pintervaltree.c passes psync_free to psync_tree_for_each_element_call_safe). */
static inline void *psync_malloc(size_t size) { return malloc(size); }
static inline void *psync_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
static inline void psync_free(void *ptr) { free(ptr); }

#endif
