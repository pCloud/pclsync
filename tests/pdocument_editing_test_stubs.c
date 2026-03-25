/* Stub implementations for pdocument_editing.c link dependencies.
 *
 * Provides controllable stubs for rwlocks, SQL, API calls, threading,
 * callbacks, settings, and string utilities. Core allocator and
 * base64 stubs come from pssl_test_stubs.c. */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>

#include "plibs.h"
#include "papi.h"
#include "pnetlibs.h"
#include "plocks.h"
#include "pcallbacks.h"
#include "psettings.h"

/* ── Auth globals (plibs.h externs) ───────────────────────────────────────── */
char psync_my_auth[64] = {0};

/* ── Thread-local error (psynclib.h extern) ───────────────────────────────── */
__thread uint32_t psync_error = 0;

/* ── Thread-local name (pcompat.h extern) ─────────────────────────────────── */
__thread const char *psync_thread_name = "test";

/* ── Timer (ptimer.h extern) ──────────────────────────────────────────────── */
time_t psync_current_time;

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

void stub_set_sql_rows(const char **rows, size_t count) {
  stub_sql_rows = rows;
  stub_sql_row_count = count;
  stub_sql_row_idx = 0;
}

void stub_reset_all(void) {
  stub_api_result = NULL;
  stub_api_last_cmd[0] = '\0';
  stub_sql_rows = NULL;
  stub_sql_row_count = 0;
  stub_sql_row_idx = 0;
  stub_last_event_id = 0;
  stub_last_event_data = NULL;
  stub_event_call_count = 0;
  stub_setting_location_id = 0;
  psync_my_auth[0] = '\0';
  psync_error = 0;
}

/* ── Debug (plibs.h) ──────────────────────────────────────────────────────── */
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

/* ── SQL stubs (plibs.h) — controllable via stub_sql_rows ─────────────────── */

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
int psync_sql_start_transaction(void)            { return 0; }
int psync_sql_commit_transaction(void)           { return 0; }
int psync_sql_rollback_transaction(void)         { return 0; }

psync_sql_res *psync_sql_prep_statement(const char *sql) {
  (void)sql;
  return (psync_sql_res *)1; /* non-NULL so callers proceed */
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
  (void)file; (void)function; (void)line;
  return psync_do_find_result(res, name, type, file, function, line);
}

/* ── Threading stubs (pcompat.h) ──────────────────────────────────────────── */
void psync_run_thread(const char *name, psync_thread_start0 func) {
  (void)name; (void)func;
  /* Do not spawn a thread in tests */
}

/* ── Callback stubs (pcallbacks.h) — controllable ─────────────────────────── */
void psync_send_eventdata(uint32_t eventid, void *data) {
  stub_last_event_id = eventid;
  stub_last_event_data = data;
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

/* ── String utilities (plibs.h) ───────────────────────────────────────────── */
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
