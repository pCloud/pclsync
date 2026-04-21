#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include "pintervaltree.h"

/* Count intervals in the tree */
static int count_intervals(psync_interval_tree_t *tree) {
  if (!tree)
    return 0;
  int n = 0;
  psync_interval_tree_t *cur;
  psync_interval_tree_for_each(cur, tree)
    n++;
  return n;
}

/* Collect from/to pairs into arrays, returns count */
static int intervals_to_arrays(psync_interval_tree_t *tree, uint64_t *froms, uint64_t *tos, int max) {
  if (!tree)
    return 0;
  int n = 0;
  psync_interval_tree_t *cur;
  psync_interval_tree_for_each(cur, tree) {
    if (n < max) {
      froms[n] = cur->from;
      tos[n] = cur->to;
    }
    n++;
  }
  return n;
}

/* Fixture */
static psync_interval_tree_t *tree;

static void setup(void) {
  tree = NULL;
}

static void teardown(void) {
  psync_interval_tree_free(tree);
  tree = NULL;
}

/* --- Add tests --- */

START_TEST(test_add_single) {
  psync_interval_tree_add(&tree, 10, 20);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 20);
}
END_TEST

START_TEST(test_add_disjoint) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_add(&tree, 30, 40);
  ck_assert_int_eq(count_intervals(tree), 2);

  uint64_t f[2], t[2];
  intervals_to_arrays(tree, f, t, 2);
  ck_assert_uint_eq(f[0], 10);
  ck_assert_uint_eq(t[0], 20);
  ck_assert_uint_eq(f[1], 30);
  ck_assert_uint_eq(t[1], 40);
}
END_TEST

START_TEST(test_add_overlap_right) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_add(&tree, 15, 30);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 30);
}
END_TEST

START_TEST(test_add_overlap_left) {
  psync_interval_tree_add(&tree, 20, 30);
  psync_interval_tree_add(&tree, 10, 25);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 30);
}
END_TEST

START_TEST(test_add_contained) {
  psync_interval_tree_add(&tree, 10, 30);
  psync_interval_tree_add(&tree, 15, 25);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 30);
}
END_TEST

START_TEST(test_add_containing) {
  psync_interval_tree_add(&tree, 15, 25);
  psync_interval_tree_add(&tree, 10, 30);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 30);
}
END_TEST

START_TEST(test_add_adjacent) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_add(&tree, 20, 30);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 30);
}
END_TEST

START_TEST(test_add_merge_multiple) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_add(&tree, 30, 40);
  psync_interval_tree_add(&tree, 50, 60);
  ck_assert_int_eq(count_intervals(tree), 3);

  /* This should merge all three */
  psync_interval_tree_add(&tree, 15, 55);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 60);
}
END_TEST

START_TEST(test_add_duplicate) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_add(&tree, 10, 20);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 20);
}
END_TEST

/* --- Remove tests --- */

START_TEST(test_remove_exact) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_remove(&tree, 10, 20);
  ck_assert_int_eq(count_intervals(tree), 0);
}
END_TEST

START_TEST(test_remove_left_trim) {
  psync_interval_tree_add(&tree, 10, 30);
  psync_interval_tree_remove(&tree, 10, 20);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 20);
  ck_assert_uint_eq(tree->to, 30);
}
END_TEST

START_TEST(test_remove_right_trim) {
  psync_interval_tree_add(&tree, 10, 30);
  psync_interval_tree_remove(&tree, 20, 30);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 20);
}
END_TEST

START_TEST(test_remove_split) {
  psync_interval_tree_add(&tree, 10, 40);
  psync_interval_tree_remove(&tree, 20, 30);
  ck_assert_int_eq(count_intervals(tree), 2);

  uint64_t f[2], t[2];
  intervals_to_arrays(tree, f, t, 2);
  ck_assert_uint_eq(f[0], 10);
  ck_assert_uint_eq(t[0], 20);
  ck_assert_uint_eq(f[1], 30);
  ck_assert_uint_eq(t[1], 40);
}
END_TEST

START_TEST(test_remove_nonoverlapping) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_remove(&tree, 30, 40);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 20);
}
END_TEST

START_TEST(test_remove_span_multiple) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_add(&tree, 30, 40);
  psync_interval_tree_add(&tree, 50, 60);
  psync_interval_tree_remove(&tree, 15, 55);

  ck_assert_int_eq(count_intervals(tree), 2);
  uint64_t f[2], t[2];
  intervals_to_arrays(tree, f, t, 2);
  ck_assert_uint_eq(f[0], 10);
  ck_assert_uint_eq(t[0], 15);
  ck_assert_uint_eq(f[1], 55);
  ck_assert_uint_eq(t[1], 60);
}
END_TEST

START_TEST(test_remove_from_empty) {
  psync_interval_tree_remove(&tree, 10, 20);
  ck_assert_int_eq(count_intervals(tree), 0);
}
END_TEST

/* --- Cut end tests --- */

START_TEST(test_cut_end_no_effect) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_cut_end(&tree, 30);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 20);
}
END_TEST

START_TEST(test_cut_end_truncate) {
  psync_interval_tree_add(&tree, 10, 30);
  psync_interval_tree_cut_end(&tree, 20);
  ck_assert_int_eq(count_intervals(tree), 1);
  ck_assert_uint_eq(tree->from, 10);
  ck_assert_uint_eq(tree->to, 20);
}
END_TEST

START_TEST(test_cut_end_remove_entirely) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_cut_end(&tree, 5);
  ck_assert_int_eq(count_intervals(tree), 0);
}
END_TEST

START_TEST(test_cut_end_multiple) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_add(&tree, 30, 40);
  psync_interval_tree_add(&tree, 50, 60);
  psync_interval_tree_cut_end(&tree, 35);

  ck_assert_int_eq(count_intervals(tree), 2);
  uint64_t f[2], t[2];
  intervals_to_arrays(tree, f, t, 2);
  ck_assert_uint_eq(f[0], 10);
  ck_assert_uint_eq(t[0], 20);
  ck_assert_uint_eq(f[1], 30);
  ck_assert_uint_eq(t[1], 35);
}
END_TEST

/* --- Search tests --- */

START_TEST(test_search_containing_or_after) {
  psync_interval_tree_add(&tree, 10, 20);
  psync_interval_tree_add(&tree, 30, 40);
  psync_interval_tree_add(&tree, 50, 60);

  /* Point before all intervals */
  psync_interval_tree_t *r = psync_interval_tree_first_interval_containing_or_after(tree, 5);
  ck_assert_ptr_ne(r, NULL);
  ck_assert_uint_eq(r->from, 10);

  /* Point inside first interval */
  r = psync_interval_tree_first_interval_containing_or_after(tree, 15);
  ck_assert_ptr_ne(r, NULL);
  ck_assert_uint_eq(r->from, 10);

  /* Point in gap between intervals */
  r = psync_interval_tree_first_interval_containing_or_after(tree, 25);
  ck_assert_ptr_ne(r, NULL);
  ck_assert_uint_eq(r->from, 30);

  /* Point at start of interval */
  r = psync_interval_tree_first_interval_containing_or_after(tree, 30);
  ck_assert_ptr_ne(r, NULL);
  ck_assert_uint_eq(r->from, 30);

  /* Point past all intervals */
  r = psync_interval_tree_first_interval_containing_or_after(tree, 60);
  ck_assert_ptr_eq(r, NULL);

  /* Point at end of last interval (exclusive end) */
  r = psync_interval_tree_first_interval_containing_or_after(tree, 59);
  ck_assert_ptr_ne(r, NULL);
  ck_assert_uint_eq(r->from, 50);
}
END_TEST

/* --- Free tests --- */

START_TEST(test_free_null) {
  psync_interval_tree_free(NULL);
  /* Should not crash */
}
END_TEST

START_TEST(test_free_populated) {
  psync_interval_tree_t *local = NULL;
  psync_interval_tree_add(&local, 10, 20);
  psync_interval_tree_add(&local, 30, 40);
  psync_interval_tree_add(&local, 50, 60);
  psync_interval_tree_free(local);
  /* Should not leak or crash */
}
END_TEST

/* --- Suite --- */

static Suite *pintervaltree_suite(void) {
  Suite *s = suite_create("pintervaltree");

  TCase *tc_add = tcase_create("add");
  tcase_add_checked_fixture(tc_add, setup, teardown);
  tcase_add_test(tc_add, test_add_single);
  tcase_add_test(tc_add, test_add_disjoint);
  tcase_add_test(tc_add, test_add_overlap_right);
  tcase_add_test(tc_add, test_add_overlap_left);
  tcase_add_test(tc_add, test_add_contained);
  tcase_add_test(tc_add, test_add_containing);
  tcase_add_test(tc_add, test_add_adjacent);
  tcase_add_test(tc_add, test_add_merge_multiple);
  tcase_add_test(tc_add, test_add_duplicate);
  suite_add_tcase(s, tc_add);

  TCase *tc_remove = tcase_create("remove");
  tcase_add_checked_fixture(tc_remove, setup, teardown);
  tcase_add_test(tc_remove, test_remove_exact);
  tcase_add_test(tc_remove, test_remove_left_trim);
  tcase_add_test(tc_remove, test_remove_right_trim);
  tcase_add_test(tc_remove, test_remove_split);
  tcase_add_test(tc_remove, test_remove_nonoverlapping);
  tcase_add_test(tc_remove, test_remove_span_multiple);
  tcase_add_test(tc_remove, test_remove_from_empty);
  suite_add_tcase(s, tc_remove);

  TCase *tc_cut = tcase_create("cut_end");
  tcase_add_checked_fixture(tc_cut, setup, teardown);
  tcase_add_test(tc_cut, test_cut_end_no_effect);
  tcase_add_test(tc_cut, test_cut_end_truncate);
  tcase_add_test(tc_cut, test_cut_end_remove_entirely);
  tcase_add_test(tc_cut, test_cut_end_multiple);
  suite_add_tcase(s, tc_cut);

  TCase *tc_search = tcase_create("search");
  tcase_add_checked_fixture(tc_search, setup, teardown);
  tcase_add_test(tc_search, test_search_containing_or_after);
  suite_add_tcase(s, tc_search);

  TCase *tc_free = tcase_create("free");
  tcase_add_test(tc_free, test_free_null);
  tcase_add_test(tc_free, test_free_populated);
  suite_add_tcase(s, tc_free);

  return s;
}

int main(void) {
  Suite *s = pintervaltree_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
