# pclsync Testability Report & Refactoring Plan

## Context

pclsync is a ~96K LOC C library with **1 test** (`check_plist.c` testing 136 LOC). The test infrastructure (Check framework + FFF mocking + header-shadowing shims in `tests/`) is proven but unexpanded. This plan proposes **implementation extractions, shim expansion, and targeted tests** to unlock unit/integration testing with minimal code changes.

---

## Progress Summary

### Completed

| Work Item | Commit | Impact |
|-----------|--------|--------|
| Test harness (Check + FFF + `check_plist.c`) | `b510608` | Test infrastructure established |
| Split `plibs.h` into 4 cohesive headers | `e2c178d` | `pcore.h`, `psql.h`, `pstrings.h`, `pencoding.h` created |
| Migrate 28 non-SQL files from `plibs.h` to `pcore.h` | `d7aaf6a` | Non-SQL files no longer pull in `<sqlite3.h>` |
| Extract `pmem.h` with `psync_memclean` | `cc42dba` | Broke `ppassword.c` dependency on `pssl.h` |
| Fix pmemlock bugs (memory leak + overflow) | `4552f4b` | 2 bugs fixed, 19 tests added |
| Fix ppassword bugs (OOB read, reversed trailing num, sqrt(0), is_punct(0)) | `1100414` | 4 bugs fixed, 17 tests added |
| Fix plocks `holding_lock` inconsistency | `3e94563` | 1 bug fixed, 11 tests added |
| Deduplicate `psync_ssl_memclean` in mbedtls/wolfssl | `fa16e6d` | SSL backends delegate to `pmem.h` |
| Test stubs: `psync_debug` return value fix | `9ac9f04` | Correct `PRINT_RETURN` behavior |
| Test shims: `pcompat.h` expanded with pthread | `d35bf35` | Enables testing pthread-dependent modules |
| Fix pcompression bugs (memory leak, Z_STREAM_END, overflow, pending, init) | `70c56f0` | 5 bugs fixed in deflate wrapper |
| Fix ptools bugs (buffer overflow, dangling ptr, missing return, bounds, stat, leak, allocator, path) | `d801e17` | 8 bugs fixed in tool functions |
| Fix ptools bugs round 2 (injection, allocator mismatch, resource leaks, TOCTOU, NULL deref) | `6dc2534` | 12 more bugs fixed |
| Test pcompression (18 tests: roundtrip, API, pending, flush, incremental) | `39f8d77` | 275 LOC covered, deflate wrapper fully tested |
| Fix psyncer bugs (taskcnt, SQL binds, path prefix, dangling ptrs) | `ca700cc` | 7 bugs fixed in sync orchestration |
| Test psyncer (26 tests: str_is_prefix, left_str_is_prefix, download list) | — | 500 LOC covered, prefix functions + AVL download list tested |
| Fix pp2p bugs (uninitialized rsapub, sendto check, pubrsa leak, INVALID_SOCKET close, PSYNC_PURE) | — | 5 bugs fixed (1 critical, 2 medium, 1 low, 1 info) |
| Fix pcloudcrypto bugs (missing else, keys_match args, weak RNG salt, buffer overflow, SHA256 header) | — | 4 bugs fixed + 1 header fix (2 critical, 1 high, 2 medium) |
| Test pp2p (11 tests: RSA key gen, socket helpers, packet processing) | — | 879 LOC covered, P2P RSA + socket + packet functions tested |
| Test pcloudcrypto (18 tests: error helpers, not-started guard, keys_match, sym key, error msg) | — | 1,968 LOC covered, crypto guard + key match + helpers tested |
| Fix pssl-openssl.h missing SHA256 defines | — | Added PSYNC_SHA256_* defines matching other SSL backends |

### Current Header Architecture

After the split, the header dependency graph looks like this:

```
plibs.h (umbrella, 34 LOC)
  +-- pcore.h (238 LOC) -- debug, variant types, globals, memory helpers
  |     +-- pcompiler.h
  |     +-- pcompat.h
  |     +-- psynclib.h
  |     +-- pstrings.h (49 LOC) -- string utility declarations
  |     +-- pencoding.h (85 LOC) -- base32/base64/hex declarations + inlines
  +-- psql.h (142 LOC) -- psync_sql_res, all psync_sql_* declarations
        +-- pcore.h
        +-- <sqlite3.h>
```

**Key outcome:** 28 files now include `pcore.h` directly (no sqlite3 dependency). Only 32 files that actually use SQL still include `plibs.h`/`psql.h`.

### What's Left

The **header declarations** are split, but the **implementations** are still monolithic in `plibs.c` (2,666 LOC). The remaining work is:
1. Extract implementations from `plibs.c` into matching `.c` files
2. Expand test shims to match the new header structure
3. Write tests for newly testable modules

---

## Current State Summary

| Metric | Value |
|--------|-------|
| Source files | 62 `.c` (77K LOC) + 69 `.h` (20K LOC) |
| Tests | 12 files (199 test cases: plist 12, ptree 20, pintervaltree 23, pcrc32c 15, pmemlock 19, ppassword 17, plocks 11, pcompression 18, papi 9, psyncer 26, pp2p 11, pcloudcrypto 18) |
| Test framework | Check + FFF (present, FFF used by pmemlock) |
| Test shims | `tests/pcompat.h`, `tests/pcore.h`, `tests/pmem.h`, `tests/psynclib.h`, `tests/pstrings.h`, `tests/pencoding.h`, `tests/plibs.h`, `tests/ptasks.h`, `tests/pstatus.h`, `tests/pfolder.h`, `tests/pdownload.h`, `tests/plocalscan.h`, `tests/plocalnotify.h`, `tests/pcallbacks.h`, `tests/ppathstatus.h`, `tests/stubs.c` |
| Header structure | `plibs.h` -> umbrella; `pcore.h` (30 files); `psql.h` via `plibs.h` (32 files) |
| Build pattern | `$(CC) check_foo.c ../foo.c -o build/check_foo -lcheck` |

### God Files (Updated)

| File | Header LOC | Impl LOC | Included by | Domains mixed |
|------|-----------|----------|-------------|---------------|
| `plibs.h` -> `pcore.h` + `psql.h` | 238 + 142 | 2,666 | 30 + 32 files | **Headers split.** Impl still has: SQL (50+ funcs), strings (7), encoding (6), debug, list builder, task mgr, pattern matching, global auth state |
| `pcompat.h/c` | 567 | 4,307 | 21 files | Sockets (30+), files (18+), paths (7), threading (4), time (5), memory (6), pipes (4), device info (4) |
| `pnetlibs.h/c` | 167 | 2,675 | 18 files | API pool (6), HTTP (8), checksums (4), speed tracking (5), file locks (3), error handling (4) |
| `psynclib.h/c` | ~1000 | 3,578 | 13 files | Public API aggregator, includes 40+ headers |

### Most-Depended-Upon Headers (Updated)

| Header | Included by | Notes |
|--------|-------------|-------|
| `pcore.h` | 30 files (directly) + 32 via `plibs.h` | Non-SQL core: debug, types, memory |
| `plibs.h` | 32 files | SQL users only (umbrella for `pcore.h` + `psql.h`) |
| `psql.h` | 1 file directly (`plibs.h`) | All SQL access goes through `plibs.h` |
| `psettings.h` | 30 files | |
| `ptimer.h` | 22 files | |
| `pcompat.h` | 21 files | |
| `pstatus.h` | 17 files | |
| `pssl.h` | 17 files | |
| `pnetlibs.h` | 16 files | |

---

## Global State Inventory (Testability Blockers)

| Variable | File:Line | Mutability | Thread-Safety | Impact |
|----------|-----------|-----------|---------------|--------|
| `psync_db` (sqlite3*) | plibs.c:123 | Mutable | Via `psync_db_lock` | CRITICAL - used by 40+ files |
| `psync_my_auth[64]` | plibs.c:117 | Mutable | Via `psync_my_auth_mutex` | HIGH |
| `psync_my_userid` | plibs.c:119 | Mutable | Via mutex | HIGH |
| `psync_status` | plibs.c:124 | Mutable | Unclear | HIGH |
| `psync_do_run` | plibs.c:125 | Mutable | No lock | CRITICAL |
| `psync_diff_run` | plibs.c:127 | Mutable | No lock | CRITICAL |
| `in_transaction` | plibs.c:135 | Mutable | Via `psync_db_lock` | HIGH |
| `apiserver[64]` | pnetlibs.c | Mutable | Via cache | HIGH |
| `download_speed`/`upload_speed` | pnetlibs.c | Mutable | No lock | MEDIUM |
| `file_lock_tree` | pnetlibs.c | Mutable | Via mutex | MEDIUM |

---

## Complete File Testability Classification

### Tier A: Testable Now (pure logic, minimal deps)

| File | LOC | Shim Needed | Status |
|------|-----|-------------|--------|
| `plist.c` | 136 | `pcompat.h` (exists) | **TESTED** (12 tests) |
| `ptree.c` | 326 | `pcore.h` shim (assert only) | **TESTED** (20 tests) |
| `pintervaltree.c` | 153 | `pcore.h` shim (malloc/free) | **TESTED** (23 tests) |
| `pcrc32c.c` | 909 | `pcore.h` shim (debug only) | **TESTED** (15 tests) |
| `pmemlock.c` | 298 | `pcompat.h` + FFF fakes | **TESTED** (19 tests, 2 bugs fixed) |

### Tier B: Testable After Implementation Extraction from `plibs.c`

| Extraction | Source File | Est. LOC | Phase |
|------------|------------|----------|-------|
| `pstrings.c` | `plibs.c` | ~200 | Phase 3 |
| `pencoding.c` | `plibs.c` | ~250 | Phase 3 |
| `pqsort.c` | `plibs.c` | ~250 | Phase 3 |
| `plistbuilder.c` | `plibs.c` | ~200 | Phase 3 |
| URL decode | `pnetlibs.c` | ~40 | Phase 6 |

### Tier C: Testable with Shim Expansion

| File | LOC | Extra Dependencies | Phase |
|------|-----|--------------------|-------|
| `plocks.c` | 383 | `pcore.h`, pthread | **TESTED** (11 tests, 1 bug fixed) |
| `ppassword.c` | 346 | `pcore.h`, `pmem.h` | **TESTED** (17 tests, 4 bugs fixed) |
| `pcompression.c` | 275 | `pcore.h`, zlib | **TESTED** (18 tests, 5 bugs fixed) |
| `ptools.c` (partial) | 1,836 | `pcore.h`, `psettings.h` | Phase 4 |
| `papi.c` | 747 | `pcore.h`, `psettings.h`, `ptimer.h` | Phase 7 |
| `pstatus.c` | 479 | `plibs.h`, `pcallbacks.h`, `ptasks.h` | Phase 7 |
| `pcallbacks.c` | 606 | `pcore.h`, `pfolder.h` | Phase 7 |
| `psettings.c` | 337 | `psql.h` (mockable) | Phase 7 |
| `ptasks.c` | 248 | `psql.h` (mockable) | Phase 7 |

### Tier D: Integration Testing Required

| File | LOC | Key Dependencies |
|------|-----|------------------|
| SQL layer (in `plibs.c`) | ~1,360 | sqlite3, plocks, pcache, ptimer |
| `pnetlibs.c` | 2,675 | pcompat (sockets), pssl, papi, pcache |
| `pcache.c` | 264 | ptimer, pssl, plist |
| `ptimer.c` | 296 | pcompat (threads), pcore |

### Tier E: System Testing Only (deeply coupled)

| File | LOC | Reason |
|------|-----|--------|
| `psynclib.c` | 3,578 | Orchestrator, includes 40+ headers |
| `pcompat.c` | 4,307 | Platform abstraction, OS-level I/O |
| `pdiff.c` | 3,581 | Network + DB + callbacks + file ops |
| `pdownload.c` | 1,738 | Network + file I/O + DB |
| `pupload.c` | 2,309 | Network + file I/O + DB |
| `plocalscan.c` | 1,590 | OS file watching + DB |
| `psyncer.c` | 513 | **PARTIAL** (26 tests: prefix funcs + download list). SQL-dependent functions untested |
| `pp2p.c` | 879 | **PARTIAL** (11 tests: RSA key gen, socket helpers, packet processing). 5 bugs fixed. Network-dependent functions untested |
| `pfs.c` | 3,788 | FUSE callbacks |
| `pfsstatic.c` | 6,759 | FUSE static filesystem |
| `ppagecache.c` | 3,665 | Cache + file I/O + crypto |
| `pcloudcrypto.c` | 1,968 | **PARTIAL** (18 tests: error helpers, not-started guard, keys_match, sym key conversion). 4 bugs fixed. Network/DB-dependent functions untested |
| `pfscrypto.c` | 1,862 | FUSE + crypto |
| `pnetlibs.c` (remaining) | ~2,500 | Sockets + SSL + API |

### Tier F: Skip (third-party / platform-specific)

| File | LOC | Reason |
|------|-----|--------|
| `miniz.c` | 7,834 | Third-party compression lib |
| `pssl-openssl.c` | 985 | SSL backend |
| `pssl-mbedtls.c` | 973 | SSL backend |
| `pssl-wolfssl.c` | 1,035 | SSL backend |
| `pssl-securetransport.c` | 534 | macOS-only |
| `pfsfake.c` | 86 | Stub (test double itself) |

---

## Refactoring Phases

### Phase 1: Test Pure Data Structures (Effort: 1-2 days)

These files now include `pcore.h` (not `plibs.h`), which means the test shim only needs to shadow `pcore.h` and its transitive includes -- no sqlite3 involvement.

#### 1.1 Test `ptree.c` (326 LOC) - AVL Balanced Tree

**Dependencies:** `ptree.h` (self-contained: only `pcompiler.h`, `<stdlib.h>`, `<stddef.h>`) and `pcore.h` (uses only `assert` macro).

**Action:** Create `tests/pcore.h` shim (see Phase 2 for content). Also requires `tests/psynclib.h`, `tests/pstrings.h`, `tests/pencoding.h` shims since `pcore.h` includes them.

**Makefile rule:**
```makefile
$(BUILD_DIR)/check_ptree: check_ptree.c ../ptree.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
```

**Test cases (~8):** add single/sorted/reverse, delete leaf/internal, traversal inorder, get_first/last, get_next/prev.

#### 1.2 Test `pintervaltree.c` (153 LOC) - Interval Tree

**Dependencies:** `pintervaltree.h`, `pcore.h` (uses `psync_new`, `psync_free`). Links with `ptree.c`.

**Makefile rule:**
```makefile
$(BUILD_DIR)/check_pintervaltree: check_pintervaltree.c ../pintervaltree.c ../ptree.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
```

**Test cases (~8):** add single/overlapping/adjacent/disjoint intervals, remove partial/exact, cut_end, search queries.

#### 1.3 Test `pcrc32c.c` (909 LOC) - CRC32C Checksum

**Dependencies:** `pcrc32c.h` (only `<stdint.h>`, `<stddef.h>`), `pcore.h` (uses `debug` in 4 places for HW detection only).

**Makefile rule:**
```makefile
$(BUILD_DIR)/check_pcrc32c: check_pcrc32c.c ../pcrc32c.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -msse4.2 $^ -o $@ $(LDFLAGS)
```

**Test cases (~6):** empty input, known CRC32C vectors, incremental vs whole, fast_hash256 determinism/uniqueness/seeded.

---

### Phase 2: Expand Shim Infrastructure (Effort: 0.5 day)

The shims need to shadow the **new** header structure (`pcore.h`, `psql.h`, `pstrings.h`, `pencoding.h`) rather than the old monolithic `plibs.h`.

#### 2.1 Expand `tests/pcompat.h` (currently 9 lines -> ~40 lines)

```c
#ifndef _PSYNC_COMPAT_H
#define _PSYNC_COMPAT_H
#include "pcompiler.h"
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

typedef unsigned long psync_uint_t;
typedef int64_t psync_int_t;
typedef int psync_file_t;
typedef int psync_socket_t;
typedef uint64_t psync_folderid_t;
typedef uint64_t psync_fileid_t;
typedef uint32_t psync_syncid_t;
typedef struct { int st_size; } psync_stat_t;
typedef struct psync_socket { int fd; } psync_socket;

#define PSYNC_THREAD __thread
extern PSYNC_THREAD const char *psync_thread_name;
#define P_PRI_I "d"
#define psync_def_var_arr(name, type, size) type name[size]
#define psync_nanotime(ts) clock_gettime(CLOCK_REALTIME, ts)
#define psync_milisleep(x) ((void)0)
#define psync_milisleep_nosqlcheck(x) ((void)0)
#define psync_stat(path, st) (-1)
#define psync_file_rename(a, b) (0)
#define psync_run_thread(name, func) ((void)0)
#define psync_run_thread1(name, func, param) ((void)0)
typedef struct psync_list { struct psync_list *next, *prev; } psync_list;
#endif
```

#### 2.2 Create `tests/psynclib.h` (new)

```c
#ifndef _PSYNC_LIB_H
#define _PSYNC_LIB_H
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef uint64_t psync_folderid_t;
typedef uint64_t psync_fileid_t;
static inline void *psync_malloc(size_t s) { return malloc(s); }
static inline void *psync_realloc(void *p, size_t s) { return realloc(p, s); }
static inline void psync_free(void *p) { free(p); }
#endif
```

#### 2.3 Create `tests/pstrings.h` (new)

Shadows `pstrings.h` with no-op or passthrough stubs for test modules that don't use string functions but transitively include them via `pcore.h`.

```c
/* Test shim for pstrings.h — provides declarations that pcore.h expects */
#ifndef _PSYNC_STRINGS_H
#define _PSYNC_STRINGS_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static inline char *psync_strdup(const char *s) { return strdup(s); }
static inline char *psync_strndup(const char *s, size_t n) { return strndup(s, n); }
/* Other string functions can be added as needed by specific tests */
#endif
```

#### 2.4 Create `tests/pencoding.h` (new)

```c
/* Test shim for pencoding.h — provides externs and macros that pcore.h expects */
#ifndef _PSYNC_ENCODING_H
#define _PSYNC_ENCODING_H
#include <stdint.h>
#include <stddef.h>
/* base64_table and __hex_lookup are defined in plibs.c; tests that need
   encoding functions will link against pencoding.c once it's extracted */
#endif
```

#### 2.5 Create `tests/pcore.h` (new) -- replaces the old `tests/plibs.h` plan

This is the universal non-SQL test shim. It shadows the real `pcore.h` that 28 non-SQL files include.

```c
#ifndef _PSYNC_CORE_H
#define _PSYNC_CORE_H
#include "pcompiler.h"
#include "pcompat.h"
#include "psynclib.h"
#include "pstrings.h"
#include "pencoding.h"
#include <string.h>

#define D_NONE 0
#define D_BUG 10
#define D_CRITICAL 20
#define D_ERROR 30
#define D_WARNING 40
#define D_NOTICE 50
#define DEBUG_LEVEL 0
#define IS_DEBUG 0
#define debug(level, ...) ((void)0)
#define assert(cond) do { if(!(cond)) abort(); } while(0)
#define assertw(cond) ((void)0)
#define debug_execute(level, expr) ((void)0)
#define likely_log(x) (x)
#define unlikely_log(x) (x)
#define TO_STR(x) #x
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))
#define psync_new(type) ((type *)psync_malloc(sizeof(type)))
#define psync_new_cnt(type, cnt) ((type *)psync_malloc(sizeof(type)*(cnt)))
#define PRINT_RETURN(x) (x)
#define PRINT_RETURN_CONST(x) (x)
#define PRINT_RETURN_FORMAT(x, ...) (x)
#define PRINT_NEG_RETURN(x) (x)
#define PRINT_NEG_RETURN_FORMAT(x, ...) (x)
#define PSYNC_THREAD __thread
extern PSYNC_THREAD uint32_t psync_error;

/* Variant types (shared with psql.h, needed by some non-SQL code too) */
#define PSYNC_TNUMBER 1
#define PSYNC_TSTRING 2
#define PSYNC_TREAL   3
#define PSYNC_TNULL   4
#define PSYNC_TBOOL   5

typedef struct {
  uint32_t type;
  uint32_t length;
  union { uint64_t num; int64_t snum; const char *str; double real; };
} psync_variant;

typedef struct { uint32_t rows; uint32_t cols; uint64_t data[]; } psync_full_result_int;
typedef const uint64_t* psync_uint_row;
typedef const char* const* psync_str_row;
typedef const psync_variant* psync_variant_row;
typedef void (*psync_run_after_t)(void *);

#define psync_get_result_cell(res, row, col) (res)->data[(row)*(res)->cols+(col)]

struct psync_list_builder_t_;
typedef struct psync_list_builder_t_ psync_list_builder_t;
typedef int (*psync_list_builder_sql_callback)(psync_list_builder_t *, void *, psync_variant_row);
typedef void (*psync_transaction_callback_t)(void *);
typedef void (*psync_task_callback_t)(void *, void *);

static inline size_t psync_strlcpy(char *dst, const char *src, size_t size) {
  size_t len = strlen(src);
  if (len < size) { memcpy(dst, src, len+1); return len; }
  else if (size) { memcpy(dst, src, size-1); dst[size-1]=0; return size-1; }
  else return 0;
}
#endif
```

#### 2.6 Create `tests/psql.h` (new) -- FFF-based SQL mock shim

This replaces the old `tests/psqldb.h` concept. Since SQL declarations now live in `psql.h`, the test shim shadows `psql.h` directly.

```c
/* Test shim for psql.h — FFF fakes for all SQL functions */
#ifndef _PSYNC_SQL_H
#define _PSYNC_SQL_H
#include "pcore.h"
#include "fff.h"

/* psync_sql_res uses void* instead of sqlite3_stmt* to avoid sqlite3 dep */
typedef struct {
  void *stmt;
  const char *sql;
  int column_count;
  int locked;
  psync_variant row[];
} psync_sql_res;

/* FFF fakes for all SQL functions */
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_connect, const char *);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_close);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_trylock);
DECLARE_FAKE_VOID_FUNC(psync_sql_lock);
DECLARE_FAKE_VOID_FUNC(psync_sql_unlock);
DECLARE_FAKE_VOID_FUNC(psync_sql_rdlock);
DECLARE_FAKE_VOID_FUNC(psync_sql_rdunlock);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_has_waiters);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_isrdlocked);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_islocked);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_tryupgradelock);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_sync);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_statement, const char *);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_start_transaction);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_commit_transaction);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_rollback_transaction);
DECLARE_FAKE_VALUE_FUNC(int64_t, psync_sql_cellint, const char *, int64_t);
DECLARE_FAKE_VALUE_FUNC(char *, psync_sql_cellstr, const char *);
DECLARE_FAKE_VALUE_FUNC(char **, psync_sql_rowstr, const char *);
DECLARE_FAKE_VALUE_FUNC(psync_variant *, psync_sql_row, const char *);
DECLARE_FAKE_VALUE_FUNC(psync_sql_res *, psync_sql_query_nocache, const char *);
DECLARE_FAKE_VALUE_FUNC(psync_sql_res *, psync_sql_query, const char *);
DECLARE_FAKE_VALUE_FUNC(psync_sql_res *, psync_sql_query_rdlock_nocache, const char *);
DECLARE_FAKE_VALUE_FUNC(psync_sql_res *, psync_sql_query_rdlock, const char *);
DECLARE_FAKE_VALUE_FUNC(psync_sql_res *, psync_sql_query_nolock, const char *);
DECLARE_FAKE_VALUE_FUNC(psync_sql_res *, psync_sql_query_nolock_nocache, const char *);
DECLARE_FAKE_VALUE_FUNC(psync_sql_res *, psync_sql_prep_statement, const char *);
DECLARE_FAKE_VALUE_FUNC(psync_sql_res *, psync_sql_prep_statement_nocache, const char *);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_reset, psync_sql_res *);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_run, psync_sql_res *);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_run_free, psync_sql_res *);
DECLARE_FAKE_VALUE_FUNC(int, psync_sql_run_free_nocache, psync_sql_res *);
DECLARE_FAKE_VOID_FUNC(psync_sql_bind_uint, psync_sql_res *, int, uint64_t);
DECLARE_FAKE_VOID_FUNC(psync_sql_bind_int, psync_sql_res *, int, int64_t);
DECLARE_FAKE_VOID_FUNC(psync_sql_bind_double, psync_sql_res *, int, double);
DECLARE_FAKE_VOID_FUNC(psync_sql_bind_string, psync_sql_res *, int, const char *);
DECLARE_FAKE_VOID_FUNC(psync_sql_bind_lstring, psync_sql_res *, int, const char *, size_t);
DECLARE_FAKE_VOID_FUNC(psync_sql_bind_blob, psync_sql_res *, int, const char *, size_t);
DECLARE_FAKE_VOID_FUNC(psync_sql_bind_null, psync_sql_res *, int);
DECLARE_FAKE_VOID_FUNC(psync_sql_free_result, psync_sql_res *);
DECLARE_FAKE_VOID_FUNC(psync_sql_free_result_nocache, psync_sql_res *);
DECLARE_FAKE_VALUE_FUNC(psync_variant_row, psync_sql_fetch_row, psync_sql_res *);
DECLARE_FAKE_VALUE_FUNC(psync_str_row, psync_sql_fetch_rowstr, psync_sql_res *);
DECLARE_FAKE_VALUE_FUNC(psync_uint_row, psync_sql_fetch_rowint, psync_sql_res *);
DECLARE_FAKE_VALUE_FUNC(psync_full_result_int *, psync_sql_fetchall_int, psync_sql_res *);
DECLARE_FAKE_VALUE_FUNC(uint32_t, psync_sql_affected_rows);
DECLARE_FAKE_VALUE_FUNC(uint64_t, psync_sql_insertid);
DECLARE_FAKE_VOID_FUNC(psync_sql_checkpoint_lock);
DECLARE_FAKE_VOID_FUNC(psync_sql_checkpoint_unlock);
DECLARE_FAKE_VOID_FUNC(psync_sql_transation_add_callbacks, psync_transaction_callback_t, psync_transaction_callback_t, void *);
DECLARE_FAKE_VOID_FUNC(psync_list_bulder_add_sql, psync_list_builder_t *, psync_sql_res *, psync_list_builder_sql_callback);
#endif
```

#### 2.7 Create `tests/plibs.h` (new) -- umbrella shim for SQL-using test targets

```c
/* Test shim for plibs.h — mirrors the real umbrella: pcore.h + psql.h */
#ifndef _PSYNC_LIBS_H
#define _PSYNC_LIBS_H
#include "pcore.h"
#include "psql.h"
#endif
```

---

### Phase 3: Extract `plibs.c` Pure Functions and Test (Effort: 3-4 days)

The headers (`pstrings.h`, `pencoding.h`) already exist with the right declarations. This phase creates the matching `.c` files by moving implementations out of `plibs.c`.

#### 3.1 Extract `pstrings.c` - String Utilities

**Move from `plibs.c`:**

| Function | plibs.c lines | Dependencies |
|----------|--------------|--------------|
| `psync_strdup` | 140-144 | `psync_malloc`, `strlen`, `memcpy` |
| `psync_strnormalize_filename` | 146-154 | `psync_malloc`, `normalize_table[]` |
| `psync_strndup` | 156-161 | `psync_malloc`, `memcpy` |
| `psync_strcat` | 163-197 | `psync_malloc`, `va_list` |
| `psync_slprintf` | 199-208 | `vsnprintf`, `unlikely_log` |
| `psync_is_valid_utf8` | 348-379 | Pure (static table) |
| `psync_match_pattern` | 1783-1815 | Pure recursive |
| `psync_ato64` | 1817-1822 | Pure |
| `psync_ato32` | 1824-1829 | Pure |
| `psync_libs_init` (normalize init) | 1750-1757 | `normalize_table[]` |
| `normalize_table[256]` | 113 | Static data |

**New file:** `pstrings.c` (header `pstrings.h` already exists)
**Update `plibs.c`:** Remove moved functions, add `#include "pstrings.h"`.
**Update `Makefile`:** Add `pstrings.c` to `SRCS`.

**Test:** `tests/check_pstrings.c`
```makefile
$(BUILD_DIR)/check_pstrings: check_pstrings.c ../pstrings.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
```

**Test cases (~19):** strdup roundtrip, strndup truncation, strcat 2 strings/many strings, slprintf normal/truncation, is_valid_utf8 ascii/multibyte/invalid, match_pattern star/question/literal/no_match, ato64/ato32 basic, strnormalize_filename, strlcpy fits/truncates.

#### 3.2 Extract `pencoding.c` - Base32/Base64/Hex

**Move from `plibs.c`:**

| Function/Data | plibs.c lines | Dependencies |
|---------------|--------------|--------------|
| `psync_base32_encode` | 210-243 | `psync_malloc` |
| `psync_base32_decode` | 246-275 | `psync_malloc`, `psync_free` |
| `psync_base64_encode` | 277-307 | `base64_table`, `psync_malloc` |
| `psync_base64_decode` | 309-346 | `base64_reverse_table`, `psync_malloc`, `psync_free` |
| `base64_table[]` | 85-91 | Static read-only data |
| `base64_reverse_table[]` | 93-110 | Static read-only data |
| `__hex_lookupl[]` | 66-83 | Static read-only data |
| `__hex_lookup` | 112 | Pointer cast of above |

**New file:** `pencoding.c` (header `pencoding.h` already exists)
**Update `Makefile`:** Add `pencoding.c` to `SRCS`.

**Test:** `tests/check_pencoding.c`
```makefile
$(BUILD_DIR)/check_pencoding: check_pencoding.c ../pencoding.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
```

**Test cases (~8):** base64 encode known/roundtrip/invalid, base32 encode known/roundtrip/invalid, binhex macro, empty inputs.

#### 3.3 Extract `pqsort.c` / `pqsort.h` - Partial Quicksort

**Extract from `plibs.c`:** The `psync_pqsort` and `psync_qpartition` functions plus their helpers (all pure, zero external dependencies).

**New files:** `pqsort.c` and `pqsort.h`
**Update `pcore.h`:** Add `#include "pqsort.h"` (or keep declarations in `pcore.h` and just create `pqsort.c`).
**Update `Makefile`:** Add `pqsort.c` to `SRCS`.

**Test cases (~7):** empty, single, sorted, reversed, random, partial sort_first < cnt, qpartition k-th element.

#### 3.4 Extract `plistbuilder.c` / `plistbuilder.h` - List Builder

**Move from `plibs.c`:**

| Function | plibs.c lines | Dependencies |
|----------|--------------|--------------|
| `psync_list_builder_create` | 1866-1884 | `psync_malloc` |
| `psync_list_bulder_add_element` | 1929-1941 | `psync_malloc` |
| `psync_list_add_string_offset` | 1973-1975 | `strlen` |
| `psync_list_add_lstring_offset` | 1943-1971 | `psync_malloc`, `memcpy` |
| `psync_list_builder_finalize` | 1977-2040+ | `psync_malloc`, `memcpy` |
| Internal structs + helpers | 1831-1904 | Type definitions |

**Note:** `psync_list_bulder_add_sql` (in `plibs.c`) depends on SQL types (`psync_sql_res`, `psync_variant_row`). It is already declared in `psql.h` (line 140). Keep its implementation in `plibs.c` (or move to `psql.c` when that extraction happens).

**New files:** `plistbuilder.c` and `plistbuilder.h`
**Update `Makefile`:** Add `plistbuilder.c` to `SRCS`.

**Test cases (~6):** create/finalize empty, add elements, add strings, finalize with data.

#### 3.5 What Remains in `plibs.c` After Phase 3

| Category | Functions | LOC (est.) |
|----------|-----------|------------|
| **SQL layer** | All `psync_sql_*` (50+ functions) | ~1,360 |
| **Debug** | `psync_debug()` | ~80 |
| **Timer wrappers** | `psync_run_after_sec`, `psync_free_after_sec` | ~30 |
| **File conflict** | `psync_rename_conflicted_file` | ~30 |
| **Task manager** | `psync_task_*` (4 functions) | ~100 |
| **Global state declarations** | Auth vars, status, psync_do_run/diff_run | ~20 |
| **Error type helpers** | `psync_err_number_expected` etc. | ~40 |
| **`psync_list_bulder_add_sql`** | SQL-dependent list builder func | ~25 |
| **`psync_try_free_memory`** | Memory pressure handler | ~15 |
| **Total remaining** | | **~1,700 LOC** |

The next major extraction (SQL layer) would bring this down to ~340 LOC.

---

### Phase 4: Test Modules Unlocked by Shims (Effort: 2-3 days)

These modules include `pcore.h` (not `plibs.h`), so the Phase 2 shims are sufficient.

#### 4.1 Test `plocks.c` (383 LOC) - Custom RWLock

**Shim:** `tests/pcore.h` from Phase 2.
**Link:** `-lpthread`

```makefile
$(BUILD_DIR)/check_plocks: check_plocks.c ../plocks.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -lpthread
```

**Test cases (~8):** init/destroy, single reader, single writer, concurrent readers, writer blocks readers, upgrade rdlock to wrlock, holding checks, waiter counting.

#### 4.2 Test `ppassword.c` (346 LOC) - Password Strength

**Shim:** `tests/pcore.h` + `tests/pssl.h` stub (fake `psync_ssl_memclean` as `memset`).

**Test cases (~6):** weak passwords, strong passwords, dictionary words, length requirements, special chars.

#### 4.3 Test `pcompression.c` (260 LOC) - Deflate Wrapper

**Shim:** `tests/pcore.h`.
**Link:** `-lz`

**Test cases (~4):** compress/decompress roundtrip, empty input, different levels.

#### 4.4 Test `ptools.c` (1,836 LOC) - String Tools (partial)

**Strategy:** Test the pure-function subset first (`psync_filename_cmp`, `psync_str_is_prefix`, formatting).
**Shim:** `tests/pcore.h` + `tests/psettings.h` stub.

**Test cases (~10):** filename comparison, prefix matching, format strings.

---

### Phase 5: Extract and Integration-Test SQL Layer (Effort: 2-3 days)

Extract the SQL implementation from `plibs.c` into `psql.c` (the header `psql.h` already exists). Then integration-test with in-memory SQLite.

#### 5.1 Create `psql.c`

**Move from `plibs.c`:**

| Code block | plibs.c lines | Description |
|------------|--------------|-------------|
| `psync_sql_err_callback` | 381-383 | SQLite error callback |
| `psync_sql_wal_checkpoint` | 385-404 | WAL checkpoint logic |
| `psync_sql_wal_hook` | 406-410 | WAL hook |
| `psync_sql_connect` | 412-504 | Database open/init |
| `psync_sql_reopen` | 506-529 | Database reopen |
| `psync_sql_checkpoint_lock/unlock` | 531-537 | Checkpoint mutex |
| Lock debugging code (IS_DEBUG) | 539-626 | `record_wrlock`, `record_rdlock`, `dump_locks` |
| `psync_sql_close` | 628-670 | Database close |
| `psync_sql_trylock/lock/unlock` | 672-740 | Write lock |
| `psync_sql_rdlock/rdunlock` | 742-798 | Read lock |
| `psync_sql_has_waiters/isrdlocked/islocked` | 800-810 | Lock queries |
| `psync_sql_tryupgradelock` | 812-832 | Lock upgrade |
| `psync_sql_sync` | 834-850 | WAL sync |
| `psync_sql_statement` | 852-876 | DDL execution |
| `psync_sql_start_transaction/commit/rollback` | 878-946 | Transaction management |
| `psync_sql_transation_add_callbacks` | 938-946 | Transaction callbacks |
| Query plan checking (debug, disabled) | 948-1026 | Debug SQL analysis |
| `psync_sql_cellstr/cellint/rowstr/row` | 1028-1210 | Simple queries |
| `psync_sql_query*` (all variants) | 1212-1394 | Query preparation |
| `psync_sql_free_result*` | 1396-1442 | Result cleanup |
| `psync_sql_prep_statement*` | 1444-1505 | Statement preparation |
| `psync_sql_reset/run/run_free*` | 1507-1571 | Statement execution |
| `psync_sql_bind_*` | 1573-1612 | Parameter binding |
| `psync_sql_fetch_*` | 1614-1708 | Row fetching |
| `psync_sql_affected_rows/insertid` | 1710-1716 | Result metadata |
| `psync_err_number_expected` etc. | ~end | Type error helpers |
| `psync_list_bulder_add_sql` | 1906-1927 | SQL list builder callback |

**Global state to move to `psql.c`:**
```
psync_rwlock_t psync_db_lock;
sqlite3 *psync_db;
static pthread_mutex_t psync_db_checkpoint_mutex;
static int in_transaction;
static int transaction_failed;
static psync_list tran_callbacks;
```

**Dependencies of `psql.c`:**
- `plocks.h` - for `psync_rwlock_*`
- `pcache.h` - for `psync_cache_get/add/clean_all`
- `ptimer.h` - for `psync_run_thread` (in WAL checkpoint)
- `pdatabase.h` - for `PSYNC_DATABASE_CONFIG`, `PSYNC_DATABASE_STRUCTURE`, `PSYNC_DATABASE_VERSION`
- `psettings.h` - for `PSYNC_DB_CHECKPOINT_AT_PAGES`, `PSYNC_QUERY_CACHE_SEC`, `PSYNC_QUERY_MAX_CNT`
- `pnetlibs.h` - for `senddebug`, `sendtdebug`, `sendassert` (debug-only)

**Update `Makefile`:** Add `psql.c` to `SRCS`.

After this, `plibs.c` shrinks from ~2,666 to ~340 LOC.

#### 5.2 Integration-Test SQL Layer

Test SQL functions with in-memory SQLite (`:memory:`).

**Additional shims needed:**

| Shim | Purpose |
|------|---------|
| `tests/pdatabase.h` | Minimal `PSYNC_DATABASE_CONFIG` and `PSYNC_DATABASE_STRUCTURE` |
| `tests/pcache.h` | No-op `psync_cache_get`/`psync_cache_add`/`psync_cache_clean_all` |
| `tests/ptimer.h` | No-op `psync_timer_register` |
| `tests/pnetlibs.h` | No-op `senddebug`/`sendtdebug`/`sendassert` |
| `tests/psettings.h` | Provide `PSYNC_DB_CHECKPOINT_AT_PAGES` |

**Link:** `psql.c` + `plocks.c` + `-lsqlite3 -lpthread`

**Test fixture:** `setup()` calls `psync_sql_connect(":memory:")`, `teardown()` calls `psync_sql_close()`.

**Test cases (~12):**
- `test_sql_connect_close` - lifecycle
- `test_sql_statement_create_table` - DDL
- `test_sql_cellint` - single integer query
- `test_sql_cellstr` - single string query
- `test_sql_prep_bind_run` - prepared statements with binds
- `test_sql_fetch_row_variant` - variant row iteration
- `test_sql_fetch_rowint` - integer row iteration
- `test_sql_fetch_rowstr` - string row iteration
- `test_sql_fetchall_int` - bulk fetch
- `test_sql_transaction_commit` - commit path
- `test_sql_transaction_rollback` - rollback path
- `test_sql_affected_rows_insertid` - row count and insert ID

#### Testing Impact

With the `tests/psql.h` FFF shim (Phase 2.6), the 32 files that include `plibs.h` for SQL access can now be tested by shadowing `psql.h` with fakes. The following modules move up in testability:

| Module | Before | After | Why |
|--------|--------|-------|-----|
| `pdiff.c` | Tier E | Tier C | Can fake all SQL calls |
| `pfolder.c` | Tier E | Tier C | Can fake SQL, test folder logic |
| `psettings.c` | Tier C | Tier B | Just SQL wrappers, easy to fake |
| `pupload.c` | Tier E | Tier D | Still needs network, but SQL is faked |
| `pdownload.c` | Tier E | Tier D | Same |
| `pstatus.c` | Tier C | Tier B | Can fake SQL + test state machine |
| `plocalscan.c` | Tier E | Tier D | Still needs FS, but SQL is faked |

---

### Phase 6: pnetlibs.c Targeted Extractions (Effort: 0.5 day+)

#### 6.1 Extract and test `psync_url_decode` (~40 LOC)

**Extract to:** `pstrings.c` (or standalone `pnetlibs_url.c`)
**Source:** `pnetlibs.c` - search for `psync_url_decode` function
**Deps:** `psync_malloc` only - pure string function

**Test cases (~4):** no encoding, `%20`/`+` spaces, special chars, malformed sequences.

#### 6.2 Future extractions (deferred)

| Extraction | Source | LOC | Benefit |
|------------|--------|-----|---------|
| `pspeed.c` | pnetlibs.c speed tracking | ~80 | Test throttling with mock time |
| `pchecksum.c` | pnetlibs.c checksums | ~120 | Test with generated files |
| `phttp.c` | pnetlibs.c HTTP ops | ~400 | Mock HTTP responses |
| `papipool.c` | pnetlibs.c API pool | ~200 | Mock API connections |

---

### Phase 7: FFF-Based Mocking for Higher-Level Modules (Effort: 3-5 days)

#### Pattern

```c
#include "fff.h"
DEFINE_FFF_GLOBALS;

/* Example: fake pnetlibs functions */
FAKE_VALUE_FUNC(psync_socket *, psync_apipool_get);
FAKE_VOID_FUNC(psync_apipool_release, psync_socket *);
FAKE_VALUE_FUNC(binresult *, psync_do_api_run_command, const char *, size_t, const binparam *, size_t);
```

#### Candidates (priority order)

| Module | LOC | What to Mock | Tests |
|--------|-----|--------------|-------|
| `papi.c` | 747 | Socket read/write | Binary protocol encode/decode |
| `pstatus.c` | 479 | Callbacks, ptasks | Status state transitions |
| `pcallbacks.c` | 606 | pfolder, pcore | Callback dispatch verification |
| `psettings.c` | 337 | SQL layer (via `tests/psql.h` FFF shim) | Settings get/set |
| `ptasks.c` | 248 | SQL layer (via `tests/psql.h` FFF shim) | Task CRUD operations |

---

## God File Deep Dive: `plibs.h` / `plibs.c`

### Current State After Header Split

`plibs.h` is now a 34-line umbrella header:
```c
#include "pcore.h"
#include "psql.h"
```

The declarations have been distributed as follows:

| Destination | What moved |
|-------------|-----------|
| `pcore.h` (238 LOC) | Debug macros, assert, variant types (`psync_variant`, accessor macros), memory helpers (`psync_new`), global externs, `psync_strlcpy` inline, list builder/task manager forward decls |
| `psql.h` (142 LOC) | `psync_sql_res` struct, all `psync_sql_*` function declarations, `#include <sqlite3.h>` |
| `pstrings.h` (49 LOC) | String utility declarations (`psync_strdup`, `psync_strcat`, `psync_match_pattern`, etc.) |
| `pencoding.h` (85 LOC) | Base32/base64/hex encoding declarations + `psync_get_string_id` inlines, `__hex_lookup`/`base64_table` externs |

### Implementation Still in `plibs.c` (2,666 LOC)

All implementations remain in `plibs.c`. The function domain map:

#### A. String Utilities (7 functions) -- **Header: `pstrings.h`, extract impl to `pstrings.c`**
```
plibs.c:140  psync_strdup
plibs.c:146  psync_strnormalize_filename
plibs.c:156  psync_strndup
plibs.c:163  psync_strcat
plibs.c:199  psync_slprintf
plibs.c:348  psync_is_valid_utf8
plibs.c:1783 psync_match_pattern
plibs.c:1817 psync_ato64
plibs.c:1824 psync_ato32
```
**Global state:** None (pure functions, only depend on `psync_malloc`)

#### B. Encoding (4 functions + data) -- **Header: `pencoding.h`, extract impl to `pencoding.c`**
```
plibs.c:210  psync_base32_encode
plibs.c:246  psync_base32_decode
plibs.c:277  psync_base64_encode
plibs.c:309  psync_base64_decode
plibs.c:66   __hex_lookupl[] (data)
plibs.c:85   base64_table[] (data)
plibs.c:93   base64_reverse_table[] (data)
```
**Global state:** `base64_table`, `__hex_lookup` (read-only static data, extern'd in `pencoding.h`)

#### C. Database/SQL (50+ functions) -- **Header: `psql.h`, extract impl to `psql.c`**
```
plibs.c:412  psync_sql_connect
plibs.c:506  psync_sql_reopen
... (50+ functions, see Phase 5.1 for full list)
plibs.c:1714 psync_sql_insertid
```
**Global state:** `psync_db`, `psync_db_lock`, `in_transaction`, `transaction_failed`, `tran_callbacks`, `psync_db_checkpoint_mutex`

#### D. List Builder (5 functions) -- **Extract to `plistbuilder.c`**
```
plibs.c:1866 psync_list_builder_create
plibs.c:1906 psync_list_bulder_add_sql  (SQL-dependent, stays with SQL or gets own file)
plibs.c:1929 psync_list_bulder_add_element
plibs.c:1943 psync_list_add_lstring_offset
plibs.c:1973 psync_list_add_string_offset
plibs.c:1977 psync_list_builder_finalize
```
**Global state:** None (builder pattern, self-contained)

#### E. Sorting (2 functions) -- **Extract to `pqsort.c`**
```
plibs.c:~2040+ psync_pqsort
plibs.c:~2040+ psync_qpartition
```
**Global state:** None (pure functions)

#### F. Kept in `plibs.c` (runtime glue)
```
plibs.c: psync_debug                    (~80 LOC)
plibs.c: psync_run_after_sec/free       (~30 LOC)
plibs.c: psync_rename_conflicted_file   (~30 LOC)
plibs.c: psync_task_* (4 functions)     (~100 LOC)
plibs.c: psync_try_free_memory          (~15 LOC)
plibs.c: psync_err_*_expected (4)       (~40 LOC)
plibs.c: Global state declarations      (~20 LOC)
```

---

## God File Deep Dive: `pcompat.h` / `pcompat.c`

### Function Domain Map

| Domain | Functions | pcompat.c approx lines | Deps |
|--------|----------|----------------------|------|
| **Platform Init** | `psync_compat_init` | early | System calls |
| **Admin/Perms** | `psync_user_is_admin`, `psync_stat_mode_ok` | early | uid/gid |
| **Paths** | `psync_get_pcloud_path`, `get_private_dir`, `get_private_tmp_dir`, `get_default_database_path`, `get_home_dir` | ~200-400 | Environment, HOME |
| **Threading** | `psync_run_thread`, `psync_run_thread1`, `psync_yield_cpu` | ~400-500 | pthread |
| **Time/Sleep** | `psync_milisleep*`, `psync_time`, `psync_nanotime`, `psync_millitime` | ~500-600 | clock_gettime |
| **Sockets** (30+) | `psync_socket_*`, `psync_create_socket`, `psync_select_in` | ~600-2000 | Socket API, SSL |
| **Pipes** | `psync_pipe*` | ~2000-2100 | pipe() |
| **Directories** | `psync_list_dir*`, `psync_check_local_dir_empty` | ~2100-2300 | opendir/readdir |
| **Files** (18+) | `psync_file_*`, `psync_mkdir`, `psync_rmdir` | ~2300-3500 | open/read/write/stat |
| **Memory** | `psync_mmap_anon*`, `psync_mlock` | ~3500-3700 | mmap |
| **Device Info** | `psync_deviceos`, `psync_device_string`, `psync_deviceid` | ~3700-3900 | uname, system files |
| **Random** | `psync_get_random_seed` | ~3900-4100 | /dev/urandom, multiple sources |
| **Misc** | `psync_rebuild_icons`, `psync_run_update_file`, `is_file_to_ignore` | ~4100-4307 | Platform-specific |

### Suggested Splits (Future, Lower Priority)

| New File | Functions | Benefit |
|----------|-----------|---------|
| `psocket.c/h` | All `psync_socket_*` (30+ funcs) | Mock sockets for network testing |
| `pfileio.c/h` | All `psync_file_*` + `psync_mkdir/rmdir` (18+ funcs) | Mock file I/O |
| `ppath.c/h` | All `psync_get_*_path` (7 funcs) | Mock path resolution |
| `pthreading.c/h` | `psync_run_thread*`, sleep, yield | Mock thread creation |
| `ptime.c/h` | `psync_time`, `psync_nanotime`, `psync_millitime` | Inject mock time |

**Note:** pcompat.c splits are lower priority than plibs.c splits because pcompat is already naturally behind the shim boundary (tests use `tests/pcompat.h` to avoid it entirely).

---

## Execution Summary

| Phase | What | Status | Tests | LOC Covered | Effort |
|-------|------|--------|-------|-------------|--------|
| ~~0~~ | ~~Test harness + check_plist.c~~ | **DONE** | 12 | 136 | -- |
| ~~0~~ | ~~Split plibs.h into 4 headers~~ | **DONE** | Infra | Enables all | -- |
| ~~0~~ | ~~Migrate 28 files to pcore.h~~ | **DONE** | Infra | 28 files freed from sqlite3 | -- |
| ~~1~~ | ~~Test ptree, pintervaltree, pcrc32c, pmemlock~~ | **DONE** | 77 | 1,686 | -- |
| ~~2~~ | ~~Shim infrastructure (pcore.h, pmem.h, psql.h, etc.)~~ | **DONE** | Infra | Enables all | -- |
| 3 | Extract pstrings.c, pencoding.c, pqsort.c, plistbuilder.c from plibs.c + test | TODO | ~40 | ~900 | 3-4 days |
| 4 | Test plocks, ppassword, pcompression, ptools | **PARTIAL** (plocks + ppassword + pcompression done, ptools bugs fixed) | 46/~56 | 1,004/~2,825 | ptools tests remaining |
| 5 | Extract SQL impl to psql.c + integration-test | TODO | ~12 | ~1,360 | 2-3 days |
| 6 | Extract/test from pnetlibs | TODO | ~4 | ~40 | 0.5 day |
| — | Fix pp2p + pcloudcrypto bugs (9 bugs) + tests (29 tests) + pssl-openssl.h SHA256 fix | **DONE** | 29 | 2,847 | -- |
| 7 | FFF-based mocking for higher-level modules | TODO | ~25 | ~2,417 | 3-5 days |
| **Total remaining** | | | **~102** | **~6,083** | **~12-18 days** |

---

## Verification

After each phase:
1. `make check` from project root - all existing + new tests pass
2. `make all` from project root - library still builds unchanged
3. For file splits: `nm build/psynclib.a | grep <symbol>` verifies no functions lost
4. For shims: `ldd build/check_ptree` verifies test binaries don't link real plibs/pcompat

## Critical Files

| File | Action |
|------|--------|
| `plibs.c` (2,666 LOC) | Extract ~2,300 LOC to 5 new files (strings + encoding + sort + list builder + SQL) |
| `pcore.h` (238 LOC) | Already done - contains non-SQL declarations |
| `psql.h` (142 LOC) | Already done - contains SQL declarations |
| `pstrings.h` (49 LOC) | Already done - contains string declarations |
| `pencoding.h` (85 LOC) | Already done - contains encoding declarations |
| `plibs.h` (34 LOC) | Already done - umbrella header |
| `Makefile` | Add new `.c` files to `SRCS` |
| `tests/Makefile` (38 LOC) | Add build rules for each new test |
| `tests/pcompat.h` (9 LOC) | Expand to ~40 LOC |
| **New:** `pstrings.c` | String utility implementations |
| **New:** `pencoding.c` | Encoding implementations |
| **New:** `pqsort.c/h` | Partial quicksort |
| **New:** `plistbuilder.c/h` | List builder pattern |
| **New:** `psql.c` | SQL/database layer implementation (highest impact) |
| **New:** `tests/pcore.h` | Universal non-SQL test shim |
| **New:** `tests/psql.h` | FFF-based SQL mock shim (unlocks 32 modules) |
| **New:** `tests/psynclib.h` | Memory allocation shim |
| **New:** `tests/pstrings.h` | String shim |
| **New:** `tests/pencoding.h` | Encoding shim |
| **New:** `tests/plibs.h` | Umbrella test shim |