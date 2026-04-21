/* Test shim for pstrings.h — provides minimal string function stubs */
#ifndef _PSYNC_STRINGS_H
#define _PSYNC_STRINGS_H

#include <stdlib.h>
#include <string.h>

static inline char *psync_strdup(const char *str) { return strdup(str); }
static inline char *psync_strndup(const char *str, size_t len) { return strndup(str, len); }

#endif
