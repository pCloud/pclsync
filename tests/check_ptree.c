#include <check.h>
#include <stdlib.h>
#include "ptree.h"
#include <string.h>

typedef struct {
  psync_tree tree;
  int key;
} test_node;

#define NODE_OF(tptr) psync_tree_element(tptr, test_node, tree)

static int node_cmp(const psync_tree *a, const psync_tree *b) {
  int ka = NODE_OF(a)->key;
  int kb = NODE_OF(b)->key;
  if (ka < kb) return -1;
  if (ka > kb) return 1;
  return 0;
}

static int check_balance(psync_tree *node) {
  if (!node) return 1;
  long int lh = psync_tree_height(node->left);
  long int rh = psync_tree_height(node->right);
  long int diff = lh - rh;
  if (diff < -1 || diff > 1) return 0;
  if (!check_balance(node->left)) return 0;
  if (!check_balance(node->right)) return 0;
  return 1;
}

/* --- Tests --- */

START_TEST(empty_tree) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  ck_assert(psync_tree_isempty(root));
  ck_assert_ptr_null(psync_tree_get_first(root));
  ck_assert_ptr_null(psync_tree_get_last(root));
  ck_assert_int_eq(psync_tree_height(root), 0);
} END_TEST

START_TEST(single_insert) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node n;
  n.key = 42;
  root = psync_tree_get_add(root, &n.tree, node_cmp);

  ck_assert(!psync_tree_isempty(root));
  ck_assert_ptr_eq(psync_tree_get_first(root), &n.tree);
  ck_assert_ptr_eq(psync_tree_get_last(root), &n.tree);
  ck_assert_int_eq(psync_tree_height(root), 1);
  ck_assert_ptr_null(psync_tree_get_next(&n.tree));
  ck_assert_ptr_null(psync_tree_get_prev(&n.tree));
} END_TEST

START_TEST(sorted_insert_100) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node nodes[100];
  int i;

  for (i = 0; i < 100; i++) {
    nodes[i].key = i + 1;
    root = psync_tree_get_add(root, &nodes[i].tree, node_cmp);
  }

  /* In-order traversal should produce sorted sequence */
  psync_tree *cur = psync_tree_get_first(root);
  ck_assert_ptr_nonnull(cur);
  int prev_key = NODE_OF(cur)->key;
  ck_assert_int_eq(prev_key, 1);
  cur = psync_tree_get_next(cur);
  i = 1;
  while (cur) {
    int k = NODE_OF(cur)->key;
    ck_assert(k > prev_key);
    prev_key = k;
    cur = psync_tree_get_next(cur);
    i++;
  }
  ck_assert_int_eq(i, 100);

  /* AVL height bound: floor(log2(100)) + 1 <= h <= 1.44*log2(101) ~ 10 */
  ck_assert_int_le(psync_tree_height(root), 10);

  /* Balance invariant at every node */
  ck_assert(check_balance(root));
} END_TEST

START_TEST(reverse_insert_100) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node nodes[100];
  int i;

  for (i = 0; i < 100; i++) {
    nodes[i].key = 100 - i;
    root = psync_tree_get_add(root, &nodes[i].tree, node_cmp);
  }

  /* First should be 1, last should be 100 */
  ck_assert_int_eq(NODE_OF(psync_tree_get_first(root))->key, 1);
  ck_assert_int_eq(NODE_OF(psync_tree_get_last(root))->key, 100);
  ck_assert_int_le(psync_tree_height(root), 10);
  ck_assert(check_balance(root));
} END_TEST

START_TEST(delete_leaf) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node nodes[5];
  int i;

  for (i = 0; i < 5; i++) {
    nodes[i].key = (i + 1) * 10;  /* 10, 20, 30, 40, 50 */
    root = psync_tree_get_add(root, &nodes[i].tree, node_cmp);
  }

  /* Delete the first node (key=10, should be a leaf or have one child) */
  psync_tree *first = psync_tree_get_first(root);
  ck_assert_int_eq(NODE_OF(first)->key, 10);
  root = psync_tree_get_del(root, first);

  /* Now first should be 20 */
  ck_assert_int_eq(NODE_OF(psync_tree_get_first(root))->key, 20);
  ck_assert(check_balance(root));

  /* Count remaining */
  int count = 0;
  psync_tree *cur;
  psync_tree_for_each(cur, root) {
    count++;
  }
  ck_assert_int_eq(count, 4);
} END_TEST

START_TEST(delete_root) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node nodes[7];
  int i;

  for (i = 0; i < 7; i++) {
    nodes[i].key = (i + 1) * 10;
    root = psync_tree_get_add(root, &nodes[i].tree, node_cmp);
  }

  /* Delete the root node */
  root = psync_tree_get_del(root, root);
  ck_assert_ptr_nonnull(root);
  ck_assert(check_balance(root));

  /* Should still have 6 nodes in sorted order */
  psync_tree *cur = psync_tree_get_first(root);
  int count = 0;
  int prev = 0;
  while (cur) {
    int k = NODE_OF(cur)->key;
    ck_assert(k > prev);
    prev = k;
    cur = psync_tree_get_next(cur);
    count++;
  }
  ck_assert_int_eq(count, 6);
} END_TEST

START_TEST(delete_node_with_two_children) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node nodes[15];
  int i;

  for (i = 0; i < 15; i++) {
    nodes[i].key = (i + 1) * 10;
    root = psync_tree_get_add(root, &nodes[i].tree, node_cmp);
  }

  /* Find node with key=80 (middle-ish, likely has two children in a 15-node AVL) */
  psync_tree *cur = psync_tree_get_first(root);
  psync_tree *target = NULL;
  while (cur) {
    if (NODE_OF(cur)->key == 80) {
      target = cur;
      break;
    }
    cur = psync_tree_get_next(cur);
  }
  ck_assert_ptr_nonnull(target);

  root = psync_tree_get_del(root, target);
  ck_assert(check_balance(root));

  /* Verify 80 is gone and rest are intact */
  int count = 0;
  cur = psync_tree_get_first(root);
  while (cur) {
    ck_assert_int_ne(NODE_OF(cur)->key, 80);
    count++;
    cur = psync_tree_get_next(cur);
  }
  ck_assert_int_eq(count, 14);
} END_TEST

START_TEST(reverse_traversal) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node nodes[10];
  int i;

  for (i = 0; i < 10; i++) {
    nodes[i].key = (i + 1) * 5;  /* 5, 10, 15, ..., 50 */
    root = psync_tree_get_add(root, &nodes[i].tree, node_cmp);
  }

  /* Traverse backward from last */
  psync_tree *cur = psync_tree_get_last(root);
  ck_assert_int_eq(NODE_OF(cur)->key, 50);

  int prev_key = 55;
  int count = 0;
  while (cur) {
    int k = NODE_OF(cur)->key;
    ck_assert(k < prev_key);
    prev_key = k;
    cur = psync_tree_get_prev(cur);
    count++;
  }
  ck_assert_int_eq(count, 10);
  ck_assert_int_eq(prev_key, 5);
} END_TEST

START_TEST(add_after_add_before) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node n1, n2, n3;
  n1.key = 20;
  n2.key = 30;
  n3.key = 10;

  /* Insert first node */
  root = psync_tree_get_add_after(root, NULL, &n1.tree);
  ck_assert_int_eq(NODE_OF(psync_tree_get_first(root))->key, 20);

  /* Insert 30 after 20 */
  root = psync_tree_get_add_after(root, &n1.tree, &n2.tree);
  ck_assert_int_eq(NODE_OF(psync_tree_get_last(root))->key, 30);

  /* Insert 10 before 20 */
  root = psync_tree_get_add_before(root, &n1.tree, &n3.tree);
  ck_assert_int_eq(NODE_OF(psync_tree_get_first(root))->key, 10);

  /* Verify order: 10, 20, 30 */
  psync_tree *cur = psync_tree_get_first(root);
  ck_assert_int_eq(NODE_OF(cur)->key, 10);
  cur = psync_tree_get_next(cur);
  ck_assert_int_eq(NODE_OF(cur)->key, 20);
  cur = psync_tree_get_next(cur);
  ck_assert_int_eq(NODE_OF(cur)->key, 30);
  ck_assert_ptr_null(psync_tree_get_next(cur));
} END_TEST

START_TEST(delete_all_nodes) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node nodes[20];
  int i;

  for (i = 0; i < 20; i++) {
    nodes[i].key = (i + 1) * 3;
    root = psync_tree_get_add(root, &nodes[i].tree, node_cmp);
  }

  /* Delete all nodes one by one from the front */
  while (!psync_tree_isempty(root)) {
    psync_tree *first = psync_tree_get_first(root);
    root = psync_tree_get_del(root, first);
    if (root)
      ck_assert(check_balance(root));
  }
  ck_assert(psync_tree_isempty(root));
} END_TEST

START_TEST(interleaved_insert_delete) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  test_node nodes[50];
  int i;

  /* Insert 50 nodes */
  for (i = 0; i < 50; i++) {
    nodes[i].key = i * 7 % 97;  /* pseudo-random via modular arithmetic */
    root = psync_tree_get_add(root, &nodes[i].tree, node_cmp);
  }
  ck_assert(check_balance(root));

  /* Delete even-indexed nodes */
  for (i = 0; i < 50; i += 2) {
    root = psync_tree_get_del(root, &nodes[i].tree);
    if (root)
      ck_assert(check_balance(root));
  }

  /* Should have 25 nodes left, still sorted */
  int count = 0;
  psync_tree *cur = psync_tree_get_first(root);
  int prev = -1;
  while (cur) {
    int k = NODE_OF(cur)->key;
    ck_assert(k > prev);
    prev = k;
    cur = psync_tree_get_next(cur);
    count++;
  }
  ck_assert_int_eq(count, 25);
} END_TEST

static Suite *ptree_suite(void) {
  Suite *s = suite_create("ptree");

  TCase *tc = tcase_create("core");
  tcase_add_test(tc, empty_tree);
  tcase_add_test(tc, single_insert);
  tcase_add_test(tc, sorted_insert_100);
  tcase_add_test(tc, reverse_insert_100);
  tcase_add_test(tc, delete_leaf);
  tcase_add_test(tc, delete_root);
  tcase_add_test(tc, delete_node_with_two_children);
  tcase_add_test(tc, reverse_traversal);
  tcase_add_test(tc, add_after_add_before);
  tcase_add_test(tc, delete_all_nodes);
  tcase_add_test(tc, interleaved_insert_delete);
  suite_add_tcase(s, tc);

  return s;
}

int main(void) {
  Suite *s = ptree_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
