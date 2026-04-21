/* Test shim for plibs.h — umbrella: pcore.h + SQL stubs */
#ifndef _PSYNC_LIBS_H
#define _PSYNC_LIBS_H
#include "pcore.h"

/* Minimal SQL types needed by modules that include plibs.h */
typedef struct { void *stmt; } psync_sql_res;
typedef const uint64_t* psync_uint_row;
typedef const char* const* psync_str_row;
typedef const psync_variant* psync_variant_row;

/* SQL function stubs — declared here, defined in per-test stub files */
psync_sql_res *psync_sql_query(const char *sql);
psync_sql_res *psync_sql_query_nolock(const char *sql);
psync_sql_res *psync_sql_prep_statement(const char *sql);
void psync_sql_bind_uint(psync_sql_res *res, int pos, uint64_t val);
void psync_sql_bind_string(psync_sql_res *res, int pos, const char *val);
int psync_sql_run(psync_sql_res *res);
int psync_sql_run_free(psync_sql_res *res);
void psync_sql_free_result(psync_sql_res *res);
psync_uint_row psync_sql_fetch_rowint(psync_sql_res *res);
psync_str_row psync_sql_fetch_rowstr(psync_sql_res *res);
psync_variant_row psync_sql_fetch_row(psync_sql_res *res);
uint32_t psync_sql_affected_rows(void);
uint64_t psync_sql_insertid(void);
int64_t psync_sql_cellint(const char *sql, int64_t dflt);
int psync_sql_statement(const char *sql);
void psync_sql_lock(void);
void psync_sql_unlock(void);
int psync_sql_start_transaction(void);
int psync_sql_commit_transaction(void);
int psync_sql_rollback_transaction(void);

/* Variant accessors */
static inline uint64_t psync_get_number(psync_variant v) { return v.num; }
static inline const char *psync_get_string(psync_variant v) { return v.str; }

#endif
