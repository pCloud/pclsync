/* Stub implementations for pdocument_editing.c link dependencies.
 *
 * Provides controllable stubs for rwlocks, SQL, API calls, threading,
 * callbacks, settings, and string utilities. Core allocator and
 * base64 stubs come from pssl_test_stubs.c.
 *
 * Headers are included via "../" prefix so the C preprocessor's
 * source-directory-first rule does not pull in the test shims that
 * exist in tests/ (plibs.h, pcore.h, pcompat.h, psettings.h, etc.). */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>

#include "../plibs.h"
#include "../papi.h"
#include "../pnetlibs.h"
#include "../plocks.h"
#include "../pcallbacks.h"
#include "../psettings.h"
#include "../pstatus.h"

/* ── Auth globals (pcore.h externs) ───────────────────────────────────────── */
char psync_my_auth[64] = {0};
pthread_mutex_t psync_my_auth_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Thread-local error (pcore.h extern) ──────────────────────────────────── */
PSYNC_THREAD uint32_t psync_error = 0;

/* ── Thread-local name (pcompat.h extern) ─────────────────────────────────── */
PSYNC_THREAD const char *psync_thread_name = "test";

/* ── Timer (ptimer.h extern) ──────────────────────────────────────────────── */
volatile time_t psync_current_time;

/* ── Controllable stub globals ────────────────────────────────────────────── */
binresult *stub_api_result = NULL;
char stub_api_last_cmd[128] = {0};

const char **stub_sql_rows = NULL;
size_t stub_sql_row_count = 0;
static size_t stub_sql_row_idx = 0;

uint32_t stub_last_event_id = 0;
void *stub_last_event_data = NULL;
int stub_event_call_count = 0;

uint64_t stub_setting_location_id = 0;

/* Sentinel object for non-NULL sql result */
static psync_sql_res stub_sql_sentinel;

/* ── binresult cleanup ────────────────────────────────────────────────────────
 * Tests build binresult trees via the make_*_response helpers, which allocate
 * each node/array separately. Production code only calls psync_free(top), so
 * the inner allocations would leak. We bridge the gap by:
 *  - collecting all sub-allocations of a tree just before returning it to the
 *    production code (so we still have valid pointers), and freeing them in
 *    stub_reset_all after the production code has freed the top;
 *  - recursively freeing any unconsumed stub_api_result in stub_reset_all. */

static void **stub_pending_frees = NULL;
static size_t stub_pending_count = 0;
static size_t stub_pending_cap   = 0;

static void stub_pending_add(void *p) {
  if (!p) return;
  if (stub_pending_count >= stub_pending_cap) {
    stub_pending_cap = stub_pending_cap ? stub_pending_cap * 2 : 32;
    stub_pending_frees = (void **)realloc(stub_pending_frees,
                                           stub_pending_cap * sizeof(void *));
  }
  stub_pending_frees[stub_pending_count++] = p;
}

static void stub_pending_flush(void) {
  size_t i;
  for (i = 0; i < stub_pending_count; i++)
    free(stub_pending_frees[i]);
  stub_pending_count = 0;
}

static void free_binresult(binresult *r);

static void collect_binresult_subtree(binresult *r) {
  uint32_t i;
  if (!r) return;
  if (r->type == PARAM_HASH && r->hash) {
    for (i = 0; i < r->length; i++)
      collect_binresult_subtree(r->hash[i].value);
    stub_pending_add(r->hash);
  } else if (r->type == PARAM_ARRAY && r->array) {
    for (i = 0; i < r->length; i++)
      collect_binresult_subtree(r->array[i]);
    stub_pending_add(r->array);
  }
  stub_pending_add(r);
}

/* Like collect_binresult_subtree but omits `r` itself — production code will
 * psync_free() the top after consuming it via the API stub. */
static void collect_binresult_children(binresult *r) {
  uint32_t i;
  if (!r) return;
  if (r->type == PARAM_HASH && r->hash) {
    for (i = 0; i < r->length; i++)
      collect_binresult_subtree(r->hash[i].value);
    stub_pending_add(r->hash);
  } else if (r->type == PARAM_ARRAY && r->array) {
    for (i = 0; i < r->length; i++)
      collect_binresult_subtree(r->array[i]);
    stub_pending_add(r->array);
  }
}

static void free_binresult(binresult *r) {
  uint32_t i;
  if (!r) return;
  if (r->type == PARAM_HASH && r->hash) {
    for (i = 0; i < r->length; i++)
      free_binresult(r->hash[i].value);
    free(r->hash);
  } else if (r->type == PARAM_ARRAY && r->array) {
    for (i = 0; i < r->length; i++)
      free_binresult(r->array[i]);
    free(r->array);
  }
  free(r);
}

void stub_set_sql_rows(const char **rows, size_t count) {
  stub_sql_rows = rows;
  stub_sql_row_count = count;
  stub_sql_row_idx = 0;
}

void stub_reset_all(void) {
  stub_pending_flush();
  if (stub_api_result) {
    free_binresult(stub_api_result);
    stub_api_result = NULL;
  }
  if (stub_last_event_data) {
    free(stub_last_event_data);
    stub_last_event_data = NULL;
  }
  stub_api_last_cmd[0] = '\0';
  stub_sql_rows = NULL;
  stub_sql_row_count = 0;
  stub_sql_row_idx = 0;
  stub_last_event_id = 0;
  stub_event_call_count = 0;
  stub_setting_location_id = 0;
  psync_my_auth[0] = '\0';
  psync_error = 0;
}

/* Ensure the last test's stub state is released before LSan scans at exit. */
__attribute__((constructor))
static void stub_register_atexit(void) {
  atexit(stub_reset_all);
}

/* ── Debug (pcore.h) ──────────────────────────────────────────────────────── */
int psync_debug(const char *file, const char *function, unsigned int line,
                unsigned int level, const char *fmt, ...) {
  (void)file; (void)function; (void)line; (void)level; (void)fmt;
  return 0;
}

/* ── Read-write lock stubs (plocks.h) ─────────────────────────────────────── */

void psync_rwlock_init(psync_rwlock_t *rw) {
  memset(rw, 0, sizeof(*rw));
  pthread_mutex_init(&rw->mutex, NULL);
}

void psync_rwlock_destroy(psync_rwlock_t *rw) {
  pthread_mutex_destroy(&rw->mutex);
}

void psync_rwlock_rdlock(psync_rwlock_t *rw) {
  pthread_mutex_lock(&rw->mutex);
}

void psync_rwlock_wrlock(psync_rwlock_t *rw) {
  pthread_mutex_lock(&rw->mutex);
}

void psync_rwlock_unlock(psync_rwlock_t *rw) {
  pthread_mutex_unlock(&rw->mutex);
}

/* ── SQL stubs (psql.h) — controllable via stub_sql_rows ──────────────────── */
/* In psql.h, names like psync_sql_query_rdlock are wrapped by file/line macros
 * only when IS_DEBUG (i.e. DEBUG_LEVEL >= D_WARNING). Tests build with
 * -DDEBUG_LEVEL=0, so the simple-named function declarations are visible
 * and the stubs below use those plain names. */

psync_sql_res *psync_sql_query_rdlock(const char *sql) {
  (void)sql;
  if (stub_sql_rows && stub_sql_row_count > 0) {
    stub_sql_row_idx = 0;
    return &stub_sql_sentinel;
  }
  return NULL;
}

psync_str_row psync_sql_fetch_rowstr(psync_sql_res *res) {
  (void)res;
  if (stub_sql_rows && stub_sql_row_idx < stub_sql_row_count) {
    /* Return pointer to the current row's string pointer.
     * psync_str_row is const char * const *, so row[0] gives the extension. */
    return &stub_sql_rows[stub_sql_row_idx++];
  }
  return NULL;
}

void psync_sql_free_result(psync_sql_res *res)   { (void)res; }
int  psync_sql_start_transaction(void)           { return 0; }
int  psync_sql_commit_transaction(void)          { return 0; }
int  psync_sql_rollback_transaction(void)        { return 0; }

psync_sql_res *psync_sql_prep_statement(const char *sql) {
  (void)sql;
  return &stub_sql_sentinel; /* non-NULL so callers proceed */
}

int psync_sql_run_free(psync_sql_res *stmt)                                     { (void)stmt; return 0; }
int psync_sql_run(psync_sql_res *stmt)                                          { (void)stmt; return 0; }
int psync_sql_reset(psync_sql_res *stmt)                                        { (void)stmt; return 0; }
void psync_sql_bind_string(psync_sql_res *stmt, int col, const char *v)         {
  (void)stmt; (void)col; (void)v;
}

/* ── API stubs (pnetlibs.h) — controllable via stub_api_result ────────────── */

binresult *psync_do_api_run_command(const char *cmd, size_t cmdlen,
                                     const binparam *params, size_t paramcnt) {
  binresult *ret;
  (void)params; (void)paramcnt;
  if (cmdlen < sizeof(stub_api_last_cmd))
    memcpy(stub_api_last_cmd, cmd, cmdlen + 1);
  else {
    memcpy(stub_api_last_cmd, cmd, sizeof(stub_api_last_cmd) - 1);
    stub_api_last_cmd[sizeof(stub_api_last_cmd) - 1] = '\0';
  }
  ret = stub_api_result;
  stub_api_result = NULL; /* consumed */
  /* Production code will psync_free(ret) — that frees only the top calloc.
   * Schedule the inner pieces for cleanup so the whole tree gets reclaimed
   * without double-freeing the top. */
  collect_binresult_children(ret);
  return ret;
}

/* ── binresult lookup — real hash-walker implementation ───────────────────── */

const binresult *psync_do_find_result(const binresult *res, const char *name,
                                       uint32_t type, const char *file,
                                       const char *function, unsigned int line) {
  uint32_t i;
  (void)file; (void)function; (void)line;
  if (!res || res->type != PARAM_HASH)
    return NULL;
  for (i = 0; i < res->length; i++)
    if (strcmp(res->hash[i].key, name) == 0 && res->hash[i].value->type == type)
      return res->hash[i].value;
  return NULL;
}

const binresult *psync_do_check_result(const binresult *res, const char *name,
                                        uint32_t type, const char *file,
                                        const char *function, unsigned int line) {
  return psync_do_find_result(res, name, type, file, function, line);
}

/* ── Threading stubs (pcompat.h) ──────────────────────────────────────────── */
void psync_run_thread(const char *name, psync_thread_start0 run) {
  (void)name; (void)run;
  /* Do not spawn a thread in tests */
}

/* ── Status wait stub (pstatus.h) ─────────────────────────────────────────── */
/* Referenced by pdocument_editing.c::refresh_thread, which is never invoked
 * because psync_run_thread is a no-op above; provided only for linking. */
void psync_wait_status(uint32_t statusid, uint32_t status) {
  (void)statusid; (void)status;
}

/* ── Callback stubs (pcallbacks.h) — controllable ─────────────────────────── */
/* Mirrors the production ownership contract (pcallbacks.c:434): the callee
 * takes ownership of eventdata. We free the previous one on overwrite and
 * the final one in stub_reset_all. Tests should read stub_last_event_data
 * but never free it. */
void psync_send_eventdata(psync_eventtype_t eventid, void *eventdata) {
  if (stub_last_event_data)
    free(stub_last_event_data);
  stub_last_event_id = eventid;
  stub_last_event_data = eventdata;
  stub_event_call_count++;
}

/* ── Settings stubs (psettings.h) — controllable ──────────────────────────── */
uint64_t psync_setting_get_uint(psync_settingid_t settingid) {
  (void)settingid;
  return stub_setting_location_id;
}

const char *psync_setting_get_string(psync_settingid_t settingid) {
  (void)settingid;
  return NULL;
}

/* ── String utilities (pstrings.h) ────────────────────────────────────────── */
char *psync_strcat(const char *str, ...) {
  const char *strs[64];
  size_t lengths[64];
  size_t i, size;
  const char *ptr;
  char *out, *p;
  va_list ap;

  strs[0] = str;
  lengths[0] = strlen(str);
  size = lengths[0] + 1;
  i = 1;

  va_start(ap, str);
  while ((ptr = va_arg(ap, const char *)) != NULL) {
    assert(i < 64);
    lengths[i] = strlen(ptr);
    strs[i] = ptr;
    size += lengths[i];
    i++;
  }
  va_end(ap);

  out = (char *)malloc(size);
  p = out;
  for (size_t j = 0; j < i; j++) {
    memcpy(p, strs[j], lengths[j]);
    p += lengths[j];
  }
  *p = '\0';
  return out;
}

/* ── Socket stubs (pcompat.h) ─────────────────────────────────────────────── */
psync_socket *psync_socket_connect(const char *host, unsigned int port, int ssl) {
  (void)host; (void)port; (void)ssl;
  return NULL;
}
void psync_socket_close(psync_socket *sock)     { (void)sock; }
void psync_socket_close_bad(psync_socket *sock) { (void)sock; }
int psync_socket_readall(psync_socket *sock, void *buff, int num) {
  (void)sock; (void)buff; (void)num;
  return -1;
}
int psync_socket_writeall(psync_socket *sock, const void *buff, int num) {
  (void)sock; (void)buff;
  return num;
}
int psync_socket_readall_thread(psync_socket *sock, void *buff, int num) {
  (void)sock; (void)buff; (void)num;
  return -1;
}
int psync_socket_writeall_thread(psync_socket *sock, const void *buff, int num) {
  (void)sock; (void)buff;
  return num;
}
int psync_socket_readall_v2(psync_socket *sock, void *buff, int num, int timeout) {
  (void)sock; (void)buff; (void)num; (void)timeout;
  return -1;
}
int psync_socket_read_noblock(psync_socket *sock, void *buff, int num) {
  (void)sock; (void)buff; (void)num;
  return -1;
}
int psync_socket_isssl(psync_socket *sock) {
  (void)sock;
  return 0;
}

time_t psync_timer_time(void) {
  return time(NULL);
}
