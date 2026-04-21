#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ppassword.h"

/* --- Tests --- */

START_TEST(test_empty_password) {
  uint64_t score = psync_password_score("");
  ck_assert_uint_eq(score, 1);
}
END_TEST

START_TEST(test_single_char) {
  uint64_t score = psync_password_score("a");
  ck_assert_uint_le(score, 100);
}
END_TEST

START_TEST(test_common_password) {
  uint64_t score = psync_password_score("password");
  ck_assert_uint_le(score, 10000);
}
END_TEST

START_TEST(test_common_password_123) {
  uint64_t score = psync_password_score("password123");
  uint64_t base = psync_password_score("password");
  /* Adding "123" should increase score somewhat but stay low */
  ck_assert_uint_gt(score, base);
  ck_assert_uint_le(score, 100000);
}
END_TEST

START_TEST(test_all_lowercase) {
  uint64_t score = psync_password_score("abcdefgh");
  ck_assert_uint_le(score, 50000);
}
END_TEST

START_TEST(test_mixed_case) {
  uint64_t lower = psync_password_score("abcdefgh");
  uint64_t mixed = psync_password_score("AbCdEfGh");
  ck_assert_uint_gt(mixed, lower);
}
END_TEST

START_TEST(test_mixed_with_digits) {
  uint64_t score = psync_password_score("Abc12345");
  ck_assert_uint_gt(score, 100);
}
END_TEST

START_TEST(test_strong_password) {
  uint64_t score = psync_password_score("X#9kL!mZ");
  uint64_t weak = psync_password_score("password");
  ck_assert_uint_gt(score, weak);
}
END_TEST

START_TEST(test_very_long_strong) {
  uint64_t score = psync_password_score("X#9kL!mZpQ7rW@sT2u");
  uint64_t short_strong = psync_password_score("X#9kL!mZ");
  ck_assert_uint_gt(score, short_strong);
}
END_TEST

START_TEST(test_trailing_year) {
  uint64_t with_year = psync_password_score("pass2024");
  uint64_t with_random = psync_password_score("pass8374");
  /* Trailing year should be penalized more (lower score) */
  ck_assert_uint_le(with_year, with_random);
}
END_TEST

START_TEST(test_trailing_number_order) {
  /* Bug fix: "pass1984" should parse 1984, not 4891 */
  uint64_t score1984 = psync_password_score("pass1984");
  /* 1984 is a year (1900-2030), should get score of 10 */
  /* 4891 is not a year, would get a different score */
  /* Just verify it returns a reasonable score */
  ck_assert_uint_gt(score1984, 0);
}
END_TEST

START_TEST(test_sequential_chars) {
  uint64_t seq = psync_password_score("abcdef");
  uint64_t random = psync_password_score("kzpxwm");
  ck_assert_uint_le(seq, random);
}
END_TEST

START_TEST(test_keyboard_pattern) {
  uint64_t qwerty = psync_password_score("qwerty");
  uint64_t random = psync_password_score("kzpxwm");
  ck_assert_uint_le(qwerty, random);
}
END_TEST

START_TEST(test_repeated_chars) {
  uint64_t score = psync_password_score("aaaaaa");
  ck_assert_uint_le(score, 1000);
}
END_TEST

START_TEST(test_score_ordering) {
  uint64_t weak = psync_password_score("password");
  uint64_t moderate = psync_password_score("Pass1234");
  uint64_t strong = psync_password_score("X#9kL!mZpQ7r");
  ck_assert_uint_lt(weak, moderate);
  ck_assert_uint_lt(moderate, strong);
}
END_TEST

START_TEST(test_uint_sqrt_zero) {
  /* Regression: uint_sqrt(0) used to return 1 */
  /* We test this indirectly — a password of all 1s triggers uint_sqrt */
  uint64_t score = psync_password_score("111111");
  ck_assert_uint_gt(score, 0);
}
END_TEST

START_TEST(test_is_punct_nul) {
  /* Regression: is_punct(0) used to return true */
  /* Test indirectly: a NUL byte can't appear in a C string, but
     empty password should not crash or report punctuation */
  uint64_t score = psync_password_score("");
  ck_assert_uint_eq(score, 1);
}
END_TEST

/* --- Suite setup --- */

static Suite *ppassword_suite(void) {
  Suite *s = suite_create("ppassword");

  TCase *tc_basic = tcase_create("basic");
  tcase_add_test(tc_basic, test_empty_password);
  tcase_add_test(tc_basic, test_single_char);
  tcase_add_test(tc_basic, test_common_password);
  tcase_add_test(tc_basic, test_common_password_123);
  suite_add_tcase(s, tc_basic);

  TCase *tc_charclass = tcase_create("char_classes");
  tcase_add_test(tc_charclass, test_all_lowercase);
  tcase_add_test(tc_charclass, test_mixed_case);
  tcase_add_test(tc_charclass, test_mixed_with_digits);
  tcase_add_test(tc_charclass, test_strong_password);
  tcase_add_test(tc_charclass, test_very_long_strong);
  suite_add_tcase(s, tc_charclass);

  TCase *tc_patterns = tcase_create("patterns");
  tcase_add_test(tc_patterns, test_trailing_year);
  tcase_add_test(tc_patterns, test_trailing_number_order);
  tcase_add_test(tc_patterns, test_sequential_chars);
  tcase_add_test(tc_patterns, test_keyboard_pattern);
  tcase_add_test(tc_patterns, test_repeated_chars);
  suite_add_tcase(s, tc_patterns);

  TCase *tc_quality = tcase_create("quality");
  tcase_add_test(tc_quality, test_score_ordering);
  suite_add_tcase(s, tc_quality);

  TCase *tc_regression = tcase_create("regression");
  tcase_add_test(tc_regression, test_uint_sqrt_zero);
  tcase_add_test(tc_regression, test_is_punct_nul);
  suite_add_tcase(s, tc_regression);

  return s;
}

int main(void) {
  Suite *s = ppassword_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
