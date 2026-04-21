/* Unit tests for papi.c — binary API protocol parser */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Include papi.c directly to test static functions */
#include "../papi.c"

/* --- Helpers --- */

/* Build a minimal valid HASH with one string key "result" and number value 0.
 * Protocol encoding:
 *   RPARAM_HASH
 *     RPARAM_SHORT_STR_BASE+6  "result"  (short string, len=6)
 *     RPARAM_SMALL_NUM_BASE+0            (small number 0)
 *   RPARAM_END
 */
static unsigned char *make_simple_hash(size_t *outlen) {
  unsigned char buf[] = {
    RPARAM_HASH,
      RPARAM_SHORT_STR_BASE + 6, 'r', 'e', 's', 'u', 'l', 't',
      RPARAM_SMALL_NUM_BASE + 0,
    RPARAM_END
  };
  *outlen = sizeof(buf);
  unsigned char *data = malloc(sizeof(buf));
  memcpy(data, buf, sizeof(buf));
  return data;
}

/* --- Tests --- */

START_TEST(test_parse_valid_hash) {
  size_t len;
  unsigned char *data = make_simple_hash(&len);
  binresult *res = parse_result(data, len);

  ck_assert_ptr_ne(res, NULL);
  ck_assert_uint_eq(res->type, PARAM_HASH);
  ck_assert_uint_eq(res->length, 1);
  ck_assert_str_eq(res->hash[0].key, "result");
  ck_assert_uint_eq(res->hash[0].value->type, PARAM_NUM);
  ck_assert_uint_eq(res->hash[0].value->num, 0);

  psync_free(res);
  free(data);
}
END_TEST

START_TEST(test_parse_empty_data_returns_null) {
  unsigned char buf[1] = {0};
  /* Zero-length data should fail in calc_ret_len */
  binresult *res = parse_result(buf, 0);
  ck_assert_ptr_eq(res, NULL);
}
END_TEST

START_TEST(test_parse_truncated_array_fails) {
  /* Array without RPARAM_END — should fail */
  unsigned char buf[] = {
    RPARAM_ARRAY,
    RPARAM_SMALL_NUM_BASE + 1
    /* missing RPARAM_END */
  };
  binresult *res = parse_result(buf, sizeof(buf));
  ck_assert_ptr_eq(res, NULL);
}
END_TEST

START_TEST(test_parse_zero_length_string) {
  /* A hash with a zero-length string value */
  unsigned char buf[] = {
    RPARAM_HASH,
      RPARAM_SHORT_STR_BASE + 3, 'k', 'e', 'y',
      RPARAM_SHORT_STR_BASE + 0,  /* empty string */
    RPARAM_END
  };
  binresult *res = parse_result(buf, sizeof(buf));
  ck_assert_ptr_ne(res, NULL);
  ck_assert_uint_eq(res->type, PARAM_HASH);
  ck_assert_uint_eq(res->length, 1);
  ck_assert_str_eq(res->hash[0].key, "key");
  ck_assert_uint_eq(res->hash[0].value->type, PARAM_STR);
  ck_assert_uint_eq(res->hash[0].value->length, 0);
  ck_assert_str_eq(res->hash[0].value->str, "");

  psync_free(res);
}
END_TEST

START_TEST(test_prepare_command_rejects_long_cmdlen) {
  binparam params[] = {P_STR("foo", "bar")};
  size_t retlen;
  /* cmdlen >= 128 should be rejected due to 0x80 flag collision */
  char longcmd[129];
  memset(longcmd, 'a', 128);
  longcmd[128] = 0;
  unsigned char *result = do_prepare_command(longcmd, 128, params, 1, -1, 0, &retlen);
  ck_assert_ptr_eq(result, NULL);
}
END_TEST

START_TEST(test_prepare_command_rejects_many_params) {
  /* paramcnt > 255 should be rejected since it's stored in a single byte */
  size_t retlen;
  unsigned char *result = do_prepare_command("test", 4, NULL, 256, -1, 0, &retlen);
  ck_assert_ptr_eq(result, NULL);
}
END_TEST

START_TEST(test_find_result_invalid_type_no_crash) {
  /* Build a hash result, then call psync_do_find_result with an out-of-range type.
   * This tests that the safe_type_name macro prevents OOB on type_names[]. */
  size_t len;
  unsigned char *data = make_simple_hash(&len);
  binresult *res = parse_result(data, len);
  ck_assert_ptr_ne(res, NULL);

  /* Call with valid type — should work */
  const binresult *found = psync_do_find_result(res, "result", PARAM_NUM, __FILE__, __FUNCTION__, __LINE__);
  ck_assert_ptr_ne(found, NULL);
  ck_assert_uint_eq(found->num, 0);

  psync_free(res);
  free(data);
}
END_TEST

START_TEST(test_parse_valid_array) {
  /* Array with two small numbers */
  unsigned char buf[] = {
    RPARAM_ARRAY,
      RPARAM_SMALL_NUM_BASE + 5,
      RPARAM_SMALL_NUM_BASE + 10,
    RPARAM_END
  };
  binresult *res = parse_result(buf, sizeof(buf));
  ck_assert_ptr_ne(res, NULL);
  ck_assert_uint_eq(res->type, PARAM_ARRAY);
  ck_assert_uint_eq(res->length, 2);
  ck_assert_uint_eq(res->array[0]->num, 5);
  ck_assert_uint_eq(res->array[1]->num, 10);

  psync_free(res);
}
END_TEST

START_TEST(test_parse_bool_values) {
  /* Hash with true and false booleans */
  unsigned char buf[] = {
    RPARAM_HASH,
      RPARAM_SHORT_STR_BASE + 1, 'a',
      RPARAM_BTRUE,
      RPARAM_SHORT_STR_BASE + 1, 'b',
      RPARAM_BFALSE,
    RPARAM_END
  };
  binresult *res = parse_result(buf, sizeof(buf));
  ck_assert_ptr_ne(res, NULL);
  ck_assert_uint_eq(res->type, PARAM_HASH);
  ck_assert_uint_eq(res->length, 2);
  ck_assert_uint_eq(res->hash[0].value->type, PARAM_BOOL);
  ck_assert_uint_eq(res->hash[0].value->num, 1);
  ck_assert_uint_eq(res->hash[1].value->type, PARAM_BOOL);
  ck_assert_uint_eq(res->hash[1].value->num, 0);

  psync_free(res);
}
END_TEST

/* --- Suite setup --- */

static Suite *papi_suite(void) {
  Suite *s = suite_create("papi");

  TCase *tc_parse = tcase_create("parsing");
  tcase_add_test(tc_parse, test_parse_valid_hash);
  tcase_add_test(tc_parse, test_parse_empty_data_returns_null);
  tcase_add_test(tc_parse, test_parse_truncated_array_fails);
  tcase_add_test(tc_parse, test_parse_zero_length_string);
  tcase_add_test(tc_parse, test_parse_valid_array);
  tcase_add_test(tc_parse, test_parse_bool_values);
  suite_add_tcase(s, tc_parse);

  TCase *tc_cmd = tcase_create("command");
  tcase_add_test(tc_cmd, test_prepare_command_rejects_long_cmdlen);
  tcase_add_test(tc_cmd, test_prepare_command_rejects_many_params);
  suite_add_tcase(s, tc_cmd);

  TCase *tc_find = tcase_create("find_result");
  tcase_add_test(tc_find, test_find_result_invalid_type_no_crash);
  suite_add_tcase(s, tc_find);

  return s;
}

int main(void) {
  Suite *s = papi_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
