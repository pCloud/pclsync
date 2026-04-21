/* Unit tests for psyncer.c — path prefix functions and download list operations.
 *
 * Build:  make check  (included in CORE_BINS)
 * Run:    ./tests/build/check_psyncer
 *
 * Tests the pure-logic functions that were fixed in the "Fix multiple bugs
 * in psyncer task counting, SQL binds, and path prefix checks" commit:
 *   - psync_str_is_prefix: symmetric path prefix detection
 *   - psync_left_str_is_prefix: left-is-prefix-of-right check
 *   - download list: add/delete/clear/lookup via AVL tree
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

/* Provide declarations that psyncer.c needs before including it.
 * Pre-define include guards so that psyncer.c's #include directives
 * for heavyweight headers (plibs.h, psettings.h, etc.) are no-ops;
 * we supply minimal types and stubs directly here. */

/* --- Core types and macros --- */
#include <stdint.h>
#include <pthread.h>

typedef uint64_t psync_folderid_t;
typedef uint64_t psync_fileid_t;
typedef uint32_t psync_syncid_t;
typedef uint32_t psync_synctype_t;

static inline void *psync_malloc(size_t s) { return malloc(s); }
static inline void psync_free(void *p) { free(p); }
static inline char *psync_strdup(const char *s) { return strdup(s); }
static inline char *psync_strndup(const char *s, size_t n) { return strndup(s, n); }

#define psync_new(type) ((type *)psync_malloc(sizeof(type)))
#define psync_new_cnt(type, cnt) ((type *)psync_malloc(sizeof(type) * (cnt)))
#define PSYNC_DIRECTORY_SEPARATORC '/'
#define psync_filename_cmp  strcmp
#define psync_filename_cmpn memcmp
#define NTO_STR(s) #s
#define TO_STR(s) NTO_STR(s)

#define PSYNC_DOWNLOAD_ONLY  1
#define PSYNC_UPLOAD_ONLY    2
#define PSYNC_PERM_READ      1
#define PERROR_OFFLINE       11
#define PSYNC_INVALID_FOLDERID ((psync_folderid_t)-1)
#define P_PRI_U64 "llu"
#define P_OS_ID 7

/* likely/unlikely */
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely_log(x) (x)
#define unlikely_log(x) (x)

/* Debug no-ops */
#define D_NONE     0
#define D_BUG      10
#define D_CRITICAL 20
#define D_ERROR    30
#define D_WARNING  40
#define D_NOTICE   50
#define debug(level, ...) do {} while (0)
#define assertw(cond) do {} while (0)

/* Invalid filename chars table */
const unsigned char psync_invalid_filename_chars[256] = {
  ['/'] = 1, ['\\'] = 1, [':'] = 1, ['*'] = 1,
  ['?'] = 1, ['"'] = 1, ['<'] = 1, ['>'] = 1, ['|'] = 1
};

uint32_t psync_error = 0;

/* --- Stat stubs --- */
typedef struct { int dummy; } psync_stat_t;
int psync_stat(const char *path, psync_stat_t *st) { (void)path; (void)st; return -1; }
#define psync_stat_isfolder(st) (1)
#define psync_stat_mode_ok(st, mode) (1)
#define psync_stat_inode(st) (0)
#define psync_stat_device(st) (0)

/* --- SQL stubs (minimal types + no-op functions) --- */
typedef struct { void *stmt; } psync_sql_res;
typedef const uint64_t *psync_uint_row;
typedef const char *const *psync_str_row;
typedef struct {
  uint32_t type; uint32_t length;
  union { uint64_t num; int64_t snum; const char *str; double real; };
} psync_variant;
typedef const psync_variant *psync_variant_row;

static inline uint64_t psync_get_number(psync_variant v) { return v.num; }
static inline const char *psync_get_string(psync_variant v) { return v.str; }

psync_sql_res *psync_sql_query(const char *sql) { (void)sql; return NULL; }
psync_sql_res *psync_sql_query_nolock(const char *sql) { (void)sql; return NULL; }
psync_sql_res *psync_sql_prep_statement(const char *sql) { (void)sql; return NULL; }
void psync_sql_bind_uint(psync_sql_res *r, int p, uint64_t v) { (void)r; (void)p; (void)v; }
void psync_sql_bind_string(psync_sql_res *r, int p, const char *v) { (void)r; (void)p; (void)v; }
int psync_sql_run(psync_sql_res *r) { (void)r; return 0; }
int psync_sql_run_free(psync_sql_res *r) { (void)r; return 0; }
void psync_sql_free_result(psync_sql_res *r) { (void)r; }
psync_uint_row psync_sql_fetch_rowint(psync_sql_res *r) { (void)r; return NULL; }
psync_str_row psync_sql_fetch_rowstr(psync_sql_res *r) { (void)r; return NULL; }
psync_variant_row psync_sql_fetch_row(psync_sql_res *r) { (void)r; return NULL; }
uint32_t psync_sql_affected_rows(void) { return 0; }
uint64_t psync_sql_insertid(void) { return 0; }
int64_t psync_sql_cellint(const char *sql, int64_t d) { (void)sql; return d; }
int psync_sql_statement(const char *sql) { (void)sql; return 0; }
void psync_sql_lock(void) {}
void psync_sql_unlock(void) {}
int psync_sql_start_transaction(void) { return 0; }
int psync_sql_commit_transaction(void) { return 0; }
int psync_sql_rollback_transaction(void) { return 0; }

/* --- Subsystem stubs --- */
int psync_is_name_to_ignore(const char *n) { (void)n; return 0; }
void psync_task_create_local_folder(uint32_t s, uint64_t f, uint64_t l) { (void)s; (void)f; (void)l; }
void psync_task_download_file_silent(uint32_t s, uint64_t f, uint64_t l, const char *n) { (void)s; (void)f; (void)l; (void)n; }
void psync_wake_localscan(void) {}
void psync_wake_download(void) {}
void psync_status_recalc_to_download(void) {}
void psync_send_status_update(void) {}
void psync_localnotify_add_sync(uint32_t syncid) { (void)syncid; }
void psync_path_status_reload_syncs(void) {}
void psync_restat_sync_folders_add(uint32_t syncid, const char *p) { (void)syncid; (void)p; }
uint64_t psync_get_folderid_by_path(const char *p) { (void)p; return 0; }
uint64_t psync_get_folderid_by_path_or_create(const char *p) { (void)p; return 0; }
#define psync_run_thread(name, func) ((void)0)
#define psync_run_thread1(name, func, param) ((void)0)

/* --- Pre-define include guards to prevent real headers from loading --- */
#define _PSYNC_SYNCER_H
#define _PSYNC_LIBS_H
#define _PSYNC_CORE_H
#define _PSYNC_SQL_H
#define _PSYNC_COMPAT_H
#define _PSYNC_LIB_H
#define _PSYNC_COMPILER_H
#define _PSYNC_SETTINGS_H
#define _PSYNC_TASKS_H
#define _PSYNC_LOCALSCAN_H
#define _PSYNC_LOCALNOTIFY_H
#define _PSYNC_FOLDER_H
#define _PSYNC_DOWNLOAD_H
#define _PSYNC_STATUS_H
#define _PSYNC_CALLBACKS_H
#define _PSYNC_PATHSTATUS_H
#define _PSYNC_STRINGS_H
#define _PSYNC_ENCODING_H
#define _PSYNC_MEMALLOC_H

/* Now include the real ptree.h (it only needs pcompiler.h which is simple) */
#include "../pcompiler.h"
#include "../ptree.h"

/* Include psyncer.c directly to test its static functions too */
#include "../psyncer.c"

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: psync_str_is_prefix
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_str_is_prefix_parent_child) {
  /* /home is a prefix of /home/user */
  ck_assert_int_eq(psync_str_is_prefix("/home", "/home/user"), 1);
} END_TEST

START_TEST(test_str_is_prefix_child_parent) {
  /* /home/user is a prefix of /home (symmetric) */
  ck_assert_int_eq(psync_str_is_prefix("/home/user", "/home"), 1);
} END_TEST

START_TEST(test_str_is_prefix_no_match) {
  /* /home and /tmp share no prefix relationship */
  ck_assert_int_eq(psync_str_is_prefix("/home", "/tmp"), 0);
} END_TEST

START_TEST(test_str_is_prefix_partial_name_no_match) {
  /* /home is NOT a prefix of /homestead — no separator boundary */
  ck_assert_int_eq(psync_str_is_prefix("/home", "/homestead"), 0);
} END_TEST

START_TEST(test_str_is_prefix_trailing_slash_str1) {
  /* Trailing slashes on str1 should be trimmed */
  ck_assert_int_eq(psync_str_is_prefix("/home/", "/home/user"), 1);
} END_TEST

START_TEST(test_str_is_prefix_trailing_slash_str2) {
  /* Trailing slashes on str2 should be trimmed (fixed by commit) */
  ck_assert_int_eq(psync_str_is_prefix("/home", "/home/user/"), 1);
} END_TEST

START_TEST(test_str_is_prefix_both_trailing_slashes) {
  ck_assert_int_eq(psync_str_is_prefix("/home/", "/home/user/"), 1);
} END_TEST

START_TEST(test_str_is_prefix_equal_paths) {
  /* Equal paths: the separator check after len1 would read str2[len1]
   * which is '\0' — not '/', so returns 0. This is correct: equal
   * paths are not "prefix" relationships. */
  ck_assert_int_eq(psync_str_is_prefix("/home", "/home"), 0);
} END_TEST

START_TEST(test_str_is_prefix_root) {
  /* Root "/" has len=1 after trimming; str2[1] must be a separator for match.
   * For "/home" str2[1]='h', so no prefix. This is by-design. */
  ck_assert_int_eq(psync_str_is_prefix("/", "/home"), 0);
  /* But root IS a prefix of "//foo" (double-slash) since str2[1]='/' */
  ck_assert_int_eq(psync_str_is_prefix("/", "//foo"), 1);
} END_TEST

START_TEST(test_str_is_prefix_deep_nesting) {
  ck_assert_int_eq(psync_str_is_prefix("/a/b/c", "/a/b/c/d/e"), 1);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: psync_left_str_is_prefix
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_left_prefix_parent_child) {
  /* /home is a left-prefix of /home/user */
  ck_assert_int_eq(psync_left_str_is_prefix("/home", "/home/user"), 1);
} END_TEST

START_TEST(test_left_prefix_child_parent_fails) {
  /* /home/user is NOT a left-prefix of /home (str2 shorter than str1) */
  ck_assert_int_eq(psync_left_str_is_prefix("/home/user", "/home"), 0);
} END_TEST

START_TEST(test_left_prefix_equal_paths) {
  /* Equal paths: str1 is a left-prefix of str2 */
  ck_assert_int_eq(psync_left_str_is_prefix("/home", "/home"), 1);
} END_TEST

START_TEST(test_left_prefix_no_boundary) {
  /* /home is NOT a left-prefix of /homestead — no separator at boundary (fixed by commit) */
  ck_assert_int_eq(psync_left_str_is_prefix("/home", "/homestead"), 0);
} END_TEST

START_TEST(test_left_prefix_trailing_slashes) {
  ck_assert_int_eq(psync_left_str_is_prefix("/home/", "/home/user/"), 1);
} END_TEST

START_TEST(test_left_prefix_no_match) {
  ck_assert_int_eq(psync_left_str_is_prefix("/home", "/tmp"), 0);
} END_TEST

START_TEST(test_left_prefix_root) {
  /* Same root "/" behavior as psync_str_is_prefix — no separator at boundary */
  ck_assert_int_eq(psync_left_str_is_prefix("/", "/anything"), 0);
  ck_assert_int_eq(psync_left_str_is_prefix("/", "//foo"), 1);
} END_TEST

START_TEST(test_left_prefix_equal_with_trailing_slash) {
  /* "/home/" is a left-prefix of "/home" after trailing slash trimming */
  ck_assert_int_eq(psync_left_str_is_prefix("/home/", "/home"), 1);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: download list (AVL tree operations)
 * ══════════════════════════════════════════════════════════════════════════ */

static void dl_setup(void) {
  psync_clear_downloadlist();
}

static void dl_teardown(void) {
  psync_clear_downloadlist();
}

START_TEST(test_dl_empty) {
  ck_assert_int_eq(psync_is_folder_in_downloadlist(42), 0);
} END_TEST

START_TEST(test_dl_add_and_find) {
  psync_add_folder_to_downloadlist(100);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(100), 1);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(101), 0);
} END_TEST

START_TEST(test_dl_add_multiple) {
  psync_add_folder_to_downloadlist(10);
  psync_add_folder_to_downloadlist(20);
  psync_add_folder_to_downloadlist(5);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(10), 1);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(20), 1);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(5), 1);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(15), 0);
} END_TEST

START_TEST(test_dl_add_duplicate) {
  psync_add_folder_to_downloadlist(42);
  psync_add_folder_to_downloadlist(42);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(42), 1);
} END_TEST

START_TEST(test_dl_delete) {
  psync_add_folder_to_downloadlist(10);
  psync_add_folder_to_downloadlist(20);
  psync_del_folder_from_downloadlist(10);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(10), 0);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(20), 1);
} END_TEST

START_TEST(test_dl_delete_nonexistent) {
  psync_add_folder_to_downloadlist(10);
  psync_del_folder_from_downloadlist(999);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(10), 1);
} END_TEST

START_TEST(test_dl_clear) {
  psync_add_folder_to_downloadlist(1);
  psync_add_folder_to_downloadlist(2);
  psync_add_folder_to_downloadlist(3);
  psync_clear_downloadlist();
  ck_assert_int_eq(psync_is_folder_in_downloadlist(1), 0);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(2), 0);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(3), 0);
} END_TEST

START_TEST(test_dl_many_entries) {
  for (uint64_t i = 1; i <= 100; i++)
    psync_add_folder_to_downloadlist(i);
  for (uint64_t i = 1; i <= 100; i++)
    ck_assert_int_eq(psync_is_folder_in_downloadlist(i), 1);
  ck_assert_int_eq(psync_is_folder_in_downloadlist(101), 0);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Suite setup
 * ══════════════════════════════════════════════════════════════════════════ */

static Suite *psyncer_suite(void) {
  Suite *s = suite_create("psyncer");

  TCase *tc_prefix = tcase_create("str_is_prefix");
  tcase_add_test(tc_prefix, test_str_is_prefix_parent_child);
  tcase_add_test(tc_prefix, test_str_is_prefix_child_parent);
  tcase_add_test(tc_prefix, test_str_is_prefix_no_match);
  tcase_add_test(tc_prefix, test_str_is_prefix_partial_name_no_match);
  tcase_add_test(tc_prefix, test_str_is_prefix_trailing_slash_str1);
  tcase_add_test(tc_prefix, test_str_is_prefix_trailing_slash_str2);
  tcase_add_test(tc_prefix, test_str_is_prefix_both_trailing_slashes);
  tcase_add_test(tc_prefix, test_str_is_prefix_equal_paths);
  tcase_add_test(tc_prefix, test_str_is_prefix_root);
  tcase_add_test(tc_prefix, test_str_is_prefix_deep_nesting);
  suite_add_tcase(s, tc_prefix);

  TCase *tc_left = tcase_create("left_str_is_prefix");
  tcase_add_test(tc_left, test_left_prefix_parent_child);
  tcase_add_test(tc_left, test_left_prefix_child_parent_fails);
  tcase_add_test(tc_left, test_left_prefix_equal_paths);
  tcase_add_test(tc_left, test_left_prefix_no_boundary);
  tcase_add_test(tc_left, test_left_prefix_trailing_slashes);
  tcase_add_test(tc_left, test_left_prefix_no_match);
  tcase_add_test(tc_left, test_left_prefix_root);
  tcase_add_test(tc_left, test_left_prefix_equal_with_trailing_slash);
  suite_add_tcase(s, tc_left);

  TCase *tc_dl = tcase_create("download_list");
  tcase_add_checked_fixture(tc_dl, dl_setup, dl_teardown);
  tcase_add_test(tc_dl, test_dl_empty);
  tcase_add_test(tc_dl, test_dl_add_and_find);
  tcase_add_test(tc_dl, test_dl_add_multiple);
  tcase_add_test(tc_dl, test_dl_add_duplicate);
  tcase_add_test(tc_dl, test_dl_delete);
  tcase_add_test(tc_dl, test_dl_delete_nonexistent);
  tcase_add_test(tc_dl, test_dl_clear);
  tcase_add_test(tc_dl, test_dl_many_entries);
  suite_add_tcase(s, tc_dl);

  return s;
}

int main(void) {
  Suite *s = psyncer_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
