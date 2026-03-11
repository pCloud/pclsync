#include <check.h>
#include <stdlib.h>
#include "plist.h"

/* Test container: a list node embedding an int value */
typedef struct {
  psync_list node;
  int val;
} int_item;

static int_item *make_item(int v) {
  int_item *it = malloc(sizeof(*it));
  it->val = v;
  return it;
}

static int cmp_int_items(const psync_list *a, const psync_list *b) {
  int va = psync_list_element(a, int_item, node)->val;
  int vb = psync_list_element(b, int_item, node)->val;
  return (va > vb) - (va < vb);
}

/* Collect list values into an array; returns count */
static int list_to_array(psync_list *head, int *out, int max) {
  psync_list *cur;
  int n = 0;
  psync_list_for_each(cur, head) {
    if (n < max)
      out[n] = psync_list_element(cur, int_item, node)->val;
    n++;
  }
  return n;
}

/* Free all items in a list */
static void free_list(psync_list *head) {
  psync_list *cur, *tmp;
  psync_list_for_each_safe(cur, tmp, head) {
    psync_list_del(cur);
    free(psync_list_element(cur, int_item, node));
  }
}

/* --- Tests --- */

START_TEST(test_list_init) {
  psync_list head;
  psync_list_init(&head);
  ck_assert_ptr_eq(head.next, &head);
  ck_assert_ptr_eq(head.prev, &head);
}
END_TEST

START_TEST(test_list_isempty) {
  psync_list head;
  psync_list_init(&head);
  ck_assert(psync_list_isempty(&head));

  int_item *it = make_item(1);
  psync_list_add_head(&head, &it->node);
  ck_assert(!psync_list_isempty(&head));

  free_list(&head);
}
END_TEST

START_TEST(test_list_add_head) {
  psync_list head;
  psync_list_init(&head);

  for (int i = 0; i < 5; i++)
    psync_list_add_head(&head, &make_item(i)->node);

  /* Head-insert reverses order: 4,3,2,1,0 */
  int vals[5];
  int n = list_to_array(&head, vals, 5);
  ck_assert_int_eq(n, 5);
  for (int i = 0; i < 5; i++)
    ck_assert_int_eq(vals[i], 4 - i);

  free_list(&head);
}
END_TEST

START_TEST(test_list_add_tail) {
  psync_list head;
  psync_list_init(&head);

  for (int i = 0; i < 5; i++)
    psync_list_add_tail(&head, &make_item(i)->node);

  /* Tail-insert preserves order: 0,1,2,3,4 */
  int vals[5];
  int n = list_to_array(&head, vals, 5);
  ck_assert_int_eq(n, 5);
  for (int i = 0; i < 5; i++)
    ck_assert_int_eq(vals[i], i);

  free_list(&head);
}
END_TEST

START_TEST(test_list_del) {
  psync_list head;
  psync_list_init(&head);

  int_item *items[3];
  for (int i = 0; i < 3; i++) {
    items[i] = make_item(i);
    psync_list_add_tail(&head, &items[i]->node);
  }

  /* Delete middle element (1) */
  psync_list_del(&items[1]->node);
  free(items[1]);

  int vals[2];
  int n = list_to_array(&head, vals, 2);
  ck_assert_int_eq(n, 2);
  ck_assert_int_eq(vals[0], 0);
  ck_assert_int_eq(vals[1], 2);

  free_list(&head);
}
END_TEST

START_TEST(test_list_remove_head) {
  psync_list head;
  psync_list_init(&head);

  for (int i = 0; i < 3; i++)
    psync_list_add_tail(&head, &make_item(i)->node);

  psync_list *removed = psync_list_remove_head(&head);
  int_item *it = psync_list_element(removed, int_item, node);
  ck_assert_int_eq(it->val, 0);
  free(it);

  int vals[2];
  int n = list_to_array(&head, vals, 2);
  ck_assert_int_eq(n, 2);
  ck_assert_int_eq(vals[0], 1);
  ck_assert_int_eq(vals[1], 2);

  free_list(&head);
}
END_TEST

START_TEST(test_list_sort_empty) {
  psync_list head;
  psync_list_init(&head);
  psync_list_sort(&head, cmp_int_items);
  ck_assert(psync_list_isempty(&head));
}
END_TEST

START_TEST(test_list_sort_single) {
  psync_list head;
  psync_list_init(&head);
  psync_list_add_tail(&head, &make_item(42)->node);

  psync_list_sort(&head, cmp_int_items);

  int vals[1];
  ck_assert_int_eq(list_to_array(&head, vals, 1), 1);
  ck_assert_int_eq(vals[0], 42);

  free_list(&head);
}
END_TEST

START_TEST(test_list_sort_sorted) {
  psync_list head;
  psync_list_init(&head);
  for (int i = 0; i < 5; i++)
    psync_list_add_tail(&head, &make_item(i)->node);

  psync_list_sort(&head, cmp_int_items);

  int vals[5];
  ck_assert_int_eq(list_to_array(&head, vals, 5), 5);
  for (int i = 0; i < 5; i++)
    ck_assert_int_eq(vals[i], i);

  free_list(&head);
}
END_TEST

START_TEST(test_list_sort_reversed) {
  psync_list head;
  psync_list_init(&head);
  for (int i = 4; i >= 0; i--)
    psync_list_add_tail(&head, &make_item(i)->node);

  psync_list_sort(&head, cmp_int_items);

  int vals[5];
  ck_assert_int_eq(list_to_array(&head, vals, 5), 5);
  for (int i = 0; i < 5; i++)
    ck_assert_int_eq(vals[i], i);

  free_list(&head);
}
END_TEST

START_TEST(test_list_sort_random) {
  psync_list head;
  psync_list_init(&head);
  int input[] = {3, 1, 4, 1, 5, 9, 2, 6};
  int expected[] = {1, 1, 2, 3, 4, 5, 6, 9};
  int count = sizeof(input) / sizeof(input[0]);

  for (int i = 0; i < count; i++)
    psync_list_add_tail(&head, &make_item(input[i])->node);

  psync_list_sort(&head, cmp_int_items);

  int vals[8];
  ck_assert_int_eq(list_to_array(&head, vals, 8), count);
  for (int i = 0; i < count; i++)
    ck_assert_int_eq(vals[i], expected[i]);

  free_list(&head);
}
END_TEST

START_TEST(test_list_extract_repeating) {
  psync_list l1, l2, ex1, ex2;
  psync_list_init(&l1);
  psync_list_init(&l2);
  psync_list_init(&ex1);
  psync_list_init(&ex2);

  /* l1 = {1, 3, 5, 7}, l2 = {2, 3, 5, 8} — common: 3, 5 */
  int v1[] = {1, 3, 5, 7};
  int v2[] = {2, 3, 5, 8};
  for (int i = 0; i < 4; i++) {
    psync_list_add_tail(&l1, &make_item(v1[i])->node);
    psync_list_add_tail(&l2, &make_item(v2[i])->node);
  }

  psync_list_extract_repeating(&l1, &l2, &ex1, &ex2, cmp_int_items);

  /* l1 should have {1, 7}, l2 should have {2, 8} */
  int vals[4];
  int n;

  n = list_to_array(&l1, vals, 4);
  ck_assert_int_eq(n, 2);
  ck_assert_int_eq(vals[0], 1);
  ck_assert_int_eq(vals[1], 7);

  n = list_to_array(&l2, vals, 4);
  ck_assert_int_eq(n, 2);
  ck_assert_int_eq(vals[0], 2);
  ck_assert_int_eq(vals[1], 8);

  /* ex1 should have {3, 5}, ex2 should have {3, 5} */
  n = list_to_array(&ex1, vals, 4);
  ck_assert_int_eq(n, 2);
  ck_assert_int_eq(vals[0], 3);
  ck_assert_int_eq(vals[1], 5);

  n = list_to_array(&ex2, vals, 4);
  ck_assert_int_eq(n, 2);
  ck_assert_int_eq(vals[0], 3);
  ck_assert_int_eq(vals[1], 5);

  free_list(&l1);
  free_list(&l2);
  free_list(&ex1);
  free_list(&ex2);
}
END_TEST

static Suite *plist_suite(void) {
  Suite *s = suite_create("plist");

  TCase *tc_basic = tcase_create("basic");
  tcase_add_test(tc_basic, test_list_init);
  tcase_add_test(tc_basic, test_list_isempty);
  tcase_add_test(tc_basic, test_list_add_head);
  tcase_add_test(tc_basic, test_list_add_tail);
  tcase_add_test(tc_basic, test_list_del);
  tcase_add_test(tc_basic, test_list_remove_head);
  suite_add_tcase(s, tc_basic);

  TCase *tc_sort = tcase_create("sort");
  tcase_add_test(tc_sort, test_list_sort_empty);
  tcase_add_test(tc_sort, test_list_sort_single);
  tcase_add_test(tc_sort, test_list_sort_sorted);
  tcase_add_test(tc_sort, test_list_sort_reversed);
  tcase_add_test(tc_sort, test_list_sort_random);
  tcase_add_test(tc_sort, test_list_extract_repeating);
  suite_add_tcase(s, tc_sort);

  return s;
}

int main(void) {
  Suite *s = plist_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
