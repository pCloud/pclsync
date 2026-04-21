/* Test shim for pmem.h — allocation macros and memclean stub */
#ifndef _PSYNC_MEM_H
#define _PSYNC_MEM_H

#include <stddef.h>
#include <string.h>

/* psync_malloc provided by stubs.c */
void *psync_malloc(size_t size);

#define psync_new(type) (type *)psync_malloc(sizeof(type))
#define psync_new_cnt(type, cnt) (type *)psync_malloc(sizeof(type)*(cnt))

/* Test stub — no need for volatile in tests */
static inline void psync_memclean(void *ptr, size_t len){
  memset(ptr, 0, len);
}

#endif
