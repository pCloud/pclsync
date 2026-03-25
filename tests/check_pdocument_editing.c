/* Unit tests for pdocument_editing.c.
 *
 * Build:  make check   (included in CORE_BINS)
 * Run:    ./tests/build/check_pdocument_editing
 *
 * Tests extension block building, cloning, the public query API,
 * URL construction, and the refresh flow.
 * Network and SQL dependencies are stubbed out. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "pdocument_editing.h"
#include "papi.h"

/* Defined in pdocument_editing_test_stubs.c */
extern char psync_my_auth[64];
extern __thread uint32_t psync_error;

extern binresult *stub_api_result;
extern char stub_api_last_cmd[128];
extern const char **stub_sql_rows;
extern size_t stub_sql_row_count;
extern uint32_t stub_last_event_id;
extern void *stub_last_event_data;
extern int stub_event_call_count;
extern uint64_t stub_setting_location_id;

extern void stub_set_sql_rows(const char **rows, size_t count);
extern void stub_reset_all(void);

/* =========================================================================
 *  binresult builder helpers
 * ========================================================================= */

static binresult *make_num_result(uint64_t val) {
  binresult *r = (binresult *)calloc(1, sizeof(binresult));
  r->type = PARAM_NUM;
  r->length = 0;
  r->num = val;
  return r;
}

static binresult *make_str_result(const char *s) {
  size_t slen = strlen(s) + 1;
  size_t extra = slen > 8 ? slen - 8 : 0;
  binresult *r = (binresult *)calloc(1, sizeof(binresult) + extra);
  r->type = PARAM_STR;
  r->length = (uint32_t)(slen - 1);
  memcpy((char *)r->str, s, slen);
  return r;
}

static binresult *make_hash_result(hashpair *pairs, uint32_t count) {
  binresult *r = (binresult *)calloc(1, sizeof(binresult));
  r->type = PARAM_HASH;
  r->length = count;
  r->hash = pairs;
  return r;
}

/* Build a getdocumentcode API response: {"result": code, "link": link} */
static binresult *make_getdocumentcode_response(uint64_t code, const char *link) {
  uint32_t n = link ? 2 : 1;
  hashpair *pairs = (hashpair *)calloc(n, sizeof(hashpair));

  pairs[0].key = "result";
  pairs[0].value = make_num_result(code);

  if (link) {
    pairs[1].key = "link";
    pairs[1].value = make_str_result(link);
  }

  return make_hash_result(pairs, n);
}

/* Build a supportedfiletypes API response:
 * {"result": code, "filetypes": {"ext1": {}, "ext2": {}, ...}} */
static binresult *make_supportedfiletypes_response(uint64_t code,
                                                    const char **exts,
                                                    size_t cnt) {
  hashpair *top_pairs;
  uint32_t top_count;

  if (cnt > 0) {
    hashpair *ft_pairs = (hashpair *)calloc(cnt, sizeof(hashpair));
    for (size_t i = 0; i < cnt; i++) {
      ft_pairs[i].key = exts[i];
      ft_pairs[i].value = make_hash_result(NULL, 0);
    }

    top_count = 2;
    top_pairs = (hashpair *)calloc(top_count, sizeof(hashpair));
    top_pairs[0].key = "result";
    top_pairs[0].value = make_num_result(code);
    top_pairs[1].key = "filetypes";
    top_pairs[1].value = make_hash_result(ft_pairs, (uint32_t)cnt);
  } else {
    top_count = 1;
    top_pairs = (hashpair *)calloc(top_count, sizeof(hashpair));
    top_pairs[0].key = "result";
    top_pairs[0].value = make_num_result(code);
  }

  return make_hash_result(top_pairs, top_count);
}

/* Recursive free for binresult trees NOT consumed by the module */
static void free_binresult(binresult *r) {
  uint32_t i;
  if (!r) return;
  if (r->type == PARAM_HASH && r->hash) {
    for (i = 0; i < r->length; i++)
      free_binresult(r->hash[i].value);
    free(r->hash);
  }
  if (r->type == PARAM_ARRAY && r->array) {
    for (i = 0; i < r->length; i++)
      free_binresult(r->array[i]);
    free(r->array);
  }
  free(r);
}

/* =========================================================================
 *  Fixture
 * ========================================================================= */

static void setup(void) {
  stub_reset_all();
}

/* =========================================================================
 *  TCase "empty_cache" — original tests
 * ========================================================================= */

START_TEST(get_extensions_empty_cache_returns_nonnull) {
  psync_document_extensions_t *exts;
  psync_do_document_editing_init();

  exts = psync_do_document_editing_get_supported_extensions();
  ck_assert_ptr_nonnull(exts);
  ck_assert_uint_eq(exts->count, 0);
  free(exts);
} END_TEST

START_TEST(get_extensions_empty_cache_twice_returns_independent_blocks) {
  psync_document_extensions_t *a, *b;
  psync_do_document_editing_init();

  a = psync_do_document_editing_get_supported_extensions();
  b = psync_do_document_editing_get_supported_extensions();
  ck_assert_ptr_nonnull(a);
  ck_assert_ptr_nonnull(b);
  ck_assert_ptr_ne(a, b);
  ck_assert_uint_eq(a->count, 0);
  ck_assert_uint_eq(b->count, 0);
  free(a);
  free(b);
} END_TEST

START_TEST(is_supported_ext_empty_cache_returns_zero) {
  psync_do_document_editing_init();
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("docx"), 0);
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext(""), 0);
} END_TEST

/* =========================================================================
 *  TCase "populated_cache" — DB-backed cache loading
 * ========================================================================= */

START_TEST(init_loads_single_ext_from_db) {
  const char *rows[] = {"docx"};
  stub_set_sql_rows(rows, 1);
  psync_do_document_editing_init();
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("docx"), 1);
} END_TEST

START_TEST(init_loads_multiple_exts_from_db) {
  const char *rows[] = {"docx", "xlsx", "pptx"};
  stub_set_sql_rows(rows, 3);
  psync_do_document_editing_init();
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("docx"), 1);
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("xlsx"), 1);
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("pptx"), 1);
} END_TEST

START_TEST(init_sorts_extensions) {
  const char *rows[] = {"xlsx", "docx", "pptx"};
  psync_document_extensions_t *exts;
  stub_set_sql_rows(rows, 3);
  psync_do_document_editing_init();
  exts = psync_do_document_editing_get_supported_extensions();
  ck_assert_uint_eq(exts->count, 3);
  ck_assert_str_eq(exts->extensions[0], "docx");
  ck_assert_str_eq(exts->extensions[1], "pptx");
  ck_assert_str_eq(exts->extensions[2], "xlsx");
  free(exts);
} END_TEST

START_TEST(is_supported_ext_unknown_returns_zero) {
  const char *rows[] = {"docx", "xlsx"};
  stub_set_sql_rows(rows, 2);
  psync_do_document_editing_init();
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("pdf"), 0);
} END_TEST

START_TEST(get_extensions_returns_correct_count) {
  const char *rows[] = {"docx", "xlsx", "pptx"};
  psync_document_extensions_t *exts;
  stub_set_sql_rows(rows, 3);
  psync_do_document_editing_init();
  exts = psync_do_document_editing_get_supported_extensions();
  ck_assert_uint_eq(exts->count, 3);
  free(exts);
} END_TEST

START_TEST(get_extensions_strings_match) {
  const char *rows[] = {"pptx", "docx", "xlsx"};
  psync_document_extensions_t *exts;
  stub_set_sql_rows(rows, 3);
  psync_do_document_editing_init();
  exts = psync_do_document_editing_get_supported_extensions();
  ck_assert_uint_eq(exts->count, 3);
  /* sorted order */
  ck_assert_str_eq(exts->extensions[0], "docx");
  ck_assert_str_eq(exts->extensions[1], "pptx");
  ck_assert_str_eq(exts->extensions[2], "xlsx");
  free(exts);
} END_TEST

/* =========================================================================
 *  TCase "url_building" — psync_do_document_editing_get_url
 * ========================================================================= */

START_TEST(get_url_no_auth_returns_null) {
  psync_do_document_editing_init();
  char *url = psync_do_document_editing_get_url(100, 0, NULL);
  ck_assert_ptr_null(url);
  ck_assert_uint_eq(psync_error, PERROR_NET_ERROR);
} END_TEST

START_TEST(get_url_api_null_returns_null) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = NULL; /* API returns NULL */
  char *url = psync_do_document_editing_get_url(100, 0, NULL);
  ck_assert_ptr_null(url);
  ck_assert_uint_eq(psync_error, PERROR_NET_ERROR);
} END_TEST

START_TEST(get_url_api_error_2003) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(2003, NULL);
  char *url = psync_do_document_editing_get_url(100, 0, NULL);
  ck_assert_ptr_null(url);
  ck_assert_uint_eq(psync_error, PERROR_ACCESS_DENIED);
} END_TEST

START_TEST(get_url_api_error_2009) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(2009, NULL);
  char *url = psync_do_document_editing_get_url(100, 0, NULL);
  ck_assert_ptr_null(url);
  ck_assert_uint_eq(psync_error, PERROR_FILE_NOT_FOUND);
} END_TEST

START_TEST(get_url_api_error_2266) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(2266, NULL);
  char *url = psync_do_document_editing_get_url(100, 0, NULL);
  ck_assert_ptr_null(url);
  ck_assert_uint_eq(psync_error, PERROR_FEATURE_DISABLED);
} END_TEST

START_TEST(get_url_success_contains_link) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(0, "https://doc.example.com/x");
  char *url = psync_do_document_editing_get_url(100, 0, NULL);
  ck_assert_ptr_nonnull(url);
  ck_assert(strstr(url, "https://doc.example.com/x") == url);
  free(url);
} END_TEST

START_TEST(get_url_null_opts_works) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(0, "https://doc.example.com/x");
  char *url = psync_do_document_editing_get_url(100, 0, NULL);
  ck_assert_ptr_nonnull(url);
  ck_assert_ptr_nonnull(strstr(url, "pcauth="));
  free(url);
} END_TEST

START_TEST(get_url_with_lang) {
  psync_document_url_opts_t opts = {.lang = "en", .theme = NULL, .gobackurl = NULL, .forcedisplay = NULL};
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(0, "https://doc.example.com/x");
  char *url = psync_do_document_editing_get_url(100, 0, &opts);
  ck_assert_ptr_nonnull(url);
  ck_assert_ptr_nonnull(strstr(url, "?lang=en"));
  free(url);
} END_TEST

START_TEST(get_url_with_theme) {
  psync_document_url_opts_t opts = {.lang = NULL, .theme = "dark", .gobackurl = NULL, .forcedisplay = NULL};
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(0, "https://doc.example.com/x");
  char *url = psync_do_document_editing_get_url(100, 0, &opts);
  ck_assert_ptr_nonnull(url);
  ck_assert_ptr_nonnull(strstr(url, "theme=dark"));
  free(url);
} END_TEST

START_TEST(get_url_with_gobackurl) {
  psync_document_url_opts_t opts = {.lang = NULL, .theme = NULL, .gobackurl = "https://back.com", .forcedisplay = NULL};
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(0, "https://doc.example.com/x");
  char *url = psync_do_document_editing_get_url(100, 0, &opts);
  ck_assert_ptr_nonnull(url);
  ck_assert_ptr_nonnull(strstr(url, "gobackurl="));
  free(url);
} END_TEST

START_TEST(get_url_with_forcedisplay) {
  psync_document_url_opts_t opts = {.lang = NULL, .theme = NULL, .gobackurl = NULL, .forcedisplay = "mobile"};
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(0, "https://doc.example.com/x");
  char *url = psync_do_document_editing_get_url(100, 0, &opts);
  ck_assert_ptr_nonnull(url);
  ck_assert_ptr_nonnull(strstr(url, "forcedisplay=mobile"));
  free(url);
} END_TEST

START_TEST(get_url_all_opts) {
  psync_document_url_opts_t opts = {.lang = "en", .theme = "dark", .gobackurl = "https://back.com", .forcedisplay = "mobile"};
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(0, "https://doc.example.com/x");
  char *url = psync_do_document_editing_get_url(100, 0, &opts);
  ck_assert_ptr_nonnull(url);
  ck_assert_ptr_nonnull(strstr(url, "lang=en"));
  ck_assert_ptr_nonnull(strstr(url, "theme=dark"));
  ck_assert_ptr_nonnull(strstr(url, "gobackurl="));
  ck_assert_ptr_nonnull(strstr(url, "forcedisplay=mobile"));
  ck_assert_ptr_nonnull(strstr(url, "pcauth=TESTAUTH"));
  /* Verify ? appears before first param, & separates others */
  ck_assert_ptr_nonnull(strstr(url, "?lang=en"));
  ck_assert_ptr_nonnull(strstr(url, "&theme=dark"));
  free(url);
} END_TEST

START_TEST(get_url_contains_pcauth) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_getdocumentcode_response(0, "https://doc.example.com/x");
  char *url = psync_do_document_editing_get_url(100, 0, NULL);
  ck_assert_ptr_nonnull(url);
  ck_assert_ptr_nonnull(strstr(url, "pcauth=TESTAUTH"));
  free(url);
} END_TEST

START_TEST(get_url_with_location_id) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_setting_location_id = 42;
  stub_api_result = make_getdocumentcode_response(0, "https://doc.example.com/x");
  char *url = psync_do_document_editing_get_url(100, 0, NULL);
  ck_assert_ptr_nonnull(url);
  ck_assert_ptr_nonnull(strstr(url, "locationid=42"));
  free(url);
} END_TEST

/* =========================================================================
 *  TCase "refresh" — psync_do_document_editing_refresh
 * ========================================================================= */

START_TEST(refresh_no_auth_returns_error) {
  psync_do_document_editing_init();
  int rc = psync_do_document_editing_refresh();
  ck_assert_int_eq(rc, -1);
} END_TEST

START_TEST(refresh_api_null_returns_error) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = NULL;
  int rc = psync_do_document_editing_refresh();
  ck_assert_int_eq(rc, -1);
} END_TEST

START_TEST(refresh_unknown_error_returns_error) {
  psync_do_document_editing_init();
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_supportedfiletypes_response(9999, NULL, 0);
  int rc = psync_do_document_editing_refresh();
  ck_assert_int_eq(rc, -1);
} END_TEST

START_TEST(refresh_2266_clears_cache) {
  /* Start with data in cache */
  const char *rows[] = {"docx", "xlsx"};
  stub_set_sql_rows(rows, 2);
  psync_do_document_editing_init();
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("docx"), 1);

  /* Refresh returns 2266 (feature disabled) */
  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_supportedfiletypes_response(2266, NULL, 0);
  int rc = psync_do_document_editing_refresh();
  ck_assert_int_eq(rc, 0);

  /* Cache should be cleared */
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("docx"), 0);
  psync_document_extensions_t *exts = psync_do_document_editing_get_supported_extensions();
  ck_assert_uint_eq(exts->count, 0);
  free(exts);
} END_TEST

START_TEST(refresh_2266_sends_empty_event) {
  const char *rows[] = {"docx"};
  stub_set_sql_rows(rows, 1);
  psync_do_document_editing_init();

  strcpy(psync_my_auth, "TESTAUTH");
  stub_api_result = make_supportedfiletypes_response(2266, NULL, 0);
  psync_do_document_editing_refresh();

  ck_assert_int_ge(stub_event_call_count, 1);
  ck_assert_uint_eq(stub_last_event_id, PEVENT_DOCUMENT_TYPES_CHANGED);
  psync_document_extensions_t *evt = (psync_document_extensions_t *)stub_last_event_data;
  ck_assert_ptr_nonnull(evt);
  ck_assert_uint_eq(evt->count, 0);
  free(evt);
} END_TEST

START_TEST(refresh_success_populates_cache) {
  psync_do_document_editing_init();
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("docx"), 0);

  strcpy(psync_my_auth, "TESTAUTH");
  const char *exts[] = {"docx", "xlsx", "pptx"};
  stub_api_result = make_supportedfiletypes_response(0, exts, 3);
  int rc = psync_do_document_editing_refresh();
  ck_assert_int_eq(rc, 0);

  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("docx"), 1);
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("xlsx"), 1);
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("pptx"), 1);
} END_TEST

START_TEST(refresh_success_sends_event) {
  psync_do_document_editing_init();

  strcpy(psync_my_auth, "TESTAUTH");
  const char *exts[] = {"docx", "xlsx"};
  stub_api_result = make_supportedfiletypes_response(0, exts, 2);
  psync_do_document_editing_refresh();

  ck_assert_int_ge(stub_event_call_count, 1);
  ck_assert_uint_eq(stub_last_event_id, PEVENT_DOCUMENT_TYPES_CHANGED);
  psync_document_extensions_t *evt = (psync_document_extensions_t *)stub_last_event_data;
  ck_assert_ptr_nonnull(evt);
  ck_assert_uint_eq(evt->count, 2);
  /* sorted */
  ck_assert_str_eq(evt->extensions[0], "docx");
  ck_assert_str_eq(evt->extensions[1], "xlsx");
  free(evt);
} END_TEST

START_TEST(refresh_no_change_skips_event) {
  /* Load "docx" from DB */
  const char *rows[] = {"docx"};
  stub_set_sql_rows(rows, 1);
  psync_do_document_editing_init();

  /* Refresh returns same extension */
  strcpy(psync_my_auth, "TESTAUTH");
  const char *exts[] = {"docx"};
  stub_api_result = make_supportedfiletypes_response(0, exts, 1);
  psync_do_document_editing_refresh();

  ck_assert_int_eq(stub_event_call_count, 0);
} END_TEST

START_TEST(refresh_replaces_old_cache) {
  /* Load "docx" from DB */
  const char *rows[] = {"docx"};
  stub_set_sql_rows(rows, 1);
  psync_do_document_editing_init();
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("docx"), 1);

  /* Refresh with new extensions */
  strcpy(psync_my_auth, "TESTAUTH");
  const char *exts[] = {"xlsx", "pptx"};
  stub_api_result = make_supportedfiletypes_response(0, exts, 2);
  int rc = psync_do_document_editing_refresh();
  ck_assert_int_eq(rc, 0);

  /* Old ext gone, new ones present */
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("docx"), 0);
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("xlsx"), 1);
  ck_assert_int_eq(psync_do_document_editing_is_supported_ext("pptx"), 1);
} END_TEST

/* =========================================================================
 *  Suite
 * ========================================================================= */

static Suite *pdocument_editing_suite(void) {
  Suite *s = suite_create("pdocument_editing");

  /* empty_cache */
  TCase *tc_empty = tcase_create("empty_cache");
  tcase_add_checked_fixture(tc_empty, setup, NULL);
  tcase_add_test(tc_empty, get_extensions_empty_cache_returns_nonnull);
  tcase_add_test(tc_empty, get_extensions_empty_cache_twice_returns_independent_blocks);
  tcase_add_test(tc_empty, is_supported_ext_empty_cache_returns_zero);
  suite_add_tcase(s, tc_empty);

  /* populated_cache */
  TCase *tc_pop = tcase_create("populated_cache");
  tcase_add_checked_fixture(tc_pop, setup, NULL);
  tcase_add_test(tc_pop, init_loads_single_ext_from_db);
  tcase_add_test(tc_pop, init_loads_multiple_exts_from_db);
  tcase_add_test(tc_pop, init_sorts_extensions);
  tcase_add_test(tc_pop, is_supported_ext_unknown_returns_zero);
  tcase_add_test(tc_pop, get_extensions_returns_correct_count);
  tcase_add_test(tc_pop, get_extensions_strings_match);
  suite_add_tcase(s, tc_pop);

  /* url_building */
  TCase *tc_url = tcase_create("url_building");
  tcase_add_checked_fixture(tc_url, setup, NULL);
  tcase_add_test(tc_url, get_url_no_auth_returns_null);
  tcase_add_test(tc_url, get_url_api_null_returns_null);
  tcase_add_test(tc_url, get_url_api_error_2003);
  tcase_add_test(tc_url, get_url_api_error_2009);
  tcase_add_test(tc_url, get_url_api_error_2266);
  tcase_add_test(tc_url, get_url_success_contains_link);
  tcase_add_test(tc_url, get_url_null_opts_works);
  tcase_add_test(tc_url, get_url_with_lang);
  tcase_add_test(tc_url, get_url_with_theme);
  tcase_add_test(tc_url, get_url_with_gobackurl);
  tcase_add_test(tc_url, get_url_with_forcedisplay);
  tcase_add_test(tc_url, get_url_all_opts);
  tcase_add_test(tc_url, get_url_contains_pcauth);
  tcase_add_test(tc_url, get_url_with_location_id);
  suite_add_tcase(s, tc_url);

  /* refresh */
  TCase *tc_refresh = tcase_create("refresh");
  tcase_add_checked_fixture(tc_refresh, setup, NULL);
  tcase_add_test(tc_refresh, refresh_no_auth_returns_error);
  tcase_add_test(tc_refresh, refresh_api_null_returns_error);
  tcase_add_test(tc_refresh, refresh_unknown_error_returns_error);
  tcase_add_test(tc_refresh, refresh_2266_clears_cache);
  tcase_add_test(tc_refresh, refresh_2266_sends_empty_event);
  tcase_add_test(tc_refresh, refresh_success_populates_cache);
  tcase_add_test(tc_refresh, refresh_success_sends_event);
  tcase_add_test(tc_refresh, refresh_no_change_skips_event);
  tcase_add_test(tc_refresh, refresh_replaces_old_cache);
  suite_add_tcase(s, tc_refresh);

  return s;
}

int main(void) {
  Suite *s = pdocument_editing_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
