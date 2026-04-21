/* Link-time stubs for library functions used by modules under test.
 * The real source files (ptree.c, pintervaltree.c, pcrc32c.c) include the
 * real headers and expect these symbols at link time. */

#include <stdarg.h>
#include <stdlib.h>

int psync_debug(const char *file, const char *function, unsigned int line,
                unsigned int level, const char *fmt, ...) {
  (void)file; (void)function; (void)line; (void)level; (void)fmt;
  return 1;
}

void *psync_malloc(size_t size) { return malloc(size); }
void *psync_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
void psync_free(void *ptr) { free(ptr); }
