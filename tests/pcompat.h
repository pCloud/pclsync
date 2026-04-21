/* Test shim for pcompat.h — provides only types needed by modules under test */
#ifndef _PSYNC_COMPAT_H
#define _PSYNC_COMPAT_H

#include "pcompiler.h"

#include <stdint.h>
#include <stddef.h>
#include <alloca.h>
#include <pthread.h>

typedef unsigned long psync_uint_t;
typedef long psync_int_t;

#define P_PRI_I "ld"
#define P_PRI_U "lu"

#define psync_def_var_arr(name, type, size) type name[size]

/* Minimal pstatus_t stub — only the typedef, no fields needed */
typedef struct {
  uint32_t status;
} pstatus_t;

/* pcompat memory functions — declared here, defined by FFF fakes or stubs */
void *psync_mmap_anon_safe(size_t size);
int psync_munmap_anon(void *ptr, size_t size);
int psync_mlock(void *ptr, size_t size);
int psync_munlock(void *ptr, size_t size);
int psync_get_page_size(void);

#define PSYNC_THREAD __thread

/* String/path macros used by psyncer.c and other modules */
#define PSYNC_DIRECTORY_SEPARATORC '/'
#define PSYNC_DIRECTORY_SEPARATOR  "/"
#define psync_filename_cmp  strcmp
#define psync_filename_cmpn memcmp
#define TO_STR(s) #s
#define NTO_STR(s) TO_STR(s)
extern const unsigned char psync_invalid_filename_chars[256];

/* stat stubs */
typedef struct { int st_size; int st_mode; } psync_stat_t;
int psync_stat(const char *path, psync_stat_t *st);
#define psync_stat_isfolder(st) (1)
#define psync_stat_mode_ok(st, mode) (1)
#define psync_stat_inode(st) (0)
#define psync_stat_device(st) (0)

/* Thread stubs */
#define psync_run_thread(name, func) ((void)0)
#define psync_run_thread1(name, func, param) ((void)0)

/* Socket types for papi.h */
typedef int psync_socket_t;
#define INVALID_SOCKET -1
#define PSYNC_SOCKET_ERROR      -1
#define PSYNC_SOCKET_WOULDBLOCK -2

typedef struct {
  psync_socket_t sock;
  int pending;
  void *ssl;
} psync_socket;

#define P_PRI_U64 "llu"
typedef uint64_t psync_userid_t;

/* Platform detection for test environment */
#if defined(__linux__)
#define P_OS_LINUX
#define P_OS_POSIX
#elif defined(__APPLE__)
#define P_OS_MACOSX
#define P_OS_BSD
#define P_OS_POSIX
#endif

#endif
