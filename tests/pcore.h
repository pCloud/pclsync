/* Test shim for pcore.h — provides debug/assert no-ops and core macros */
#ifndef _PSYNC_CORE_H
#define _PSYNC_CORE_H

#include "pcompiler.h"
#include "pcompat.h"
#include "psynclib.h"
#include "pstrings.h"
#include "pencoding.h"

#include <string.h>
#include <assert.h>

/* Debug levels */
#define D_NONE     0
#define D_BUG      10
#define D_CRITICAL 20
#define D_ERROR    30
#define D_WARNING  40
#define D_NOTICE   50

/* debug() as no-op */
#define debug(level, ...) do {} while (0)

/* assert() uses real assert from <assert.h> (already included) */
#undef assert
#define assert(cond) do { if (!(cond)) abort(); } while (0)
#define assertw(cond) do {} while (0)
#define debug_execute(level, expr) do {} while (0)

/* likely_log/unlikely_log as passthrough */
#define likely_log(x)   likely(x)
#define unlikely_log(x) unlikely(x)

/* PRINT_RETURN family as identity macros */
#define PRINT_RETURN(x) (x)
#define PRINT_RETURN_CONST(x) (x)
#define PRINT_RETURN_FORMAT(x, format, ...) (x)
#define PRINT_NEG_RETURN(x) (x)
#define PRINT_NEG_RETURN_FORMAT(x, format, ...) (x)

/* Variant types */
#define PSYNC_TNUMBER 1
#define PSYNC_TSTRING 2
#define PSYNC_TREAL   3
#define PSYNC_TNULL   4
#define PSYNC_TBOOL   5

typedef struct {
  uint32_t type;
  uint32_t length;
  union {
    uint64_t num;
    int64_t snum;
    const char *str;
    double real;
  };
} psync_variant;

/* Allocation convenience macros + psync_memclean */
#include "pmem.h"

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))

/* Stub psync_debug for any code that takes its address */
static inline int psync_debug(const char *file, const char *function,
    unsigned int line, unsigned int level, const char *fmt, ...) {
  (void)file; (void)function; (void)line; (void)level; (void)fmt;
  return 1;
}

static inline size_t psync_strlcpy(char *dst, const char *src, size_t size) {
  size_t len = strlen(src);
  if (len < size) {
    memcpy(dst, src, len + 1);
    return len;
  } else if (size) {
    memcpy(dst, src, size - 1);
    dst[size - 1] = 0;
    return size - 1;
  } else
    return 0;
}

/* Globals referenced by pcore.h — declared but not defined (not needed) */
extern psync_folderid_t lost_and_found_fid;

#endif
