#include <check.h>
#include <stdlib.h>
#include "ptree.h"

/* Test container: a tree node embedding an int key */
typedef struct {
  psync_tree node;
  int key;
} int_node;

static int cmp_int_nodes(const psync_tree *a, const psync_tree *b) {
  int ka = psync_tree_element(a, int_node, node)->key;
  int kb = psync_tree_element(b, int_node, node)->key;
  return (ka > kb) - (ka < kb);
}

/* Verify AVL invariant: returns height if valid, -1 if violated */
static long verify_avl(psync_tree *t) {
  if (!t)
    return 0;
  long lh = verify_avl(t->left);
  long rh = verify_avl(t->right);
  if (lh < 0 || rh < 0)
    return -1;
  long diff = lh - rh;
  if (diff < -1 || diff > 1)
    return -1;
  long expected = (lh > rh ? lh : rh) + 1;
  if (t->height != expected)
    return -1;
  /* Check parent pointers */
  if (t->left && t->left->parent != t)
    return -1;
  if (t->right && t->right->parent != t)
    return -1;
  return expected;
}

/* Collect inorder keys into array, returns count */
static int tree_to_array(psync_tree *root, int *out, int max) {
  psync_tree *cur;
  int n = 0;
  psync_tree_for_each(cur, root) {
    if (n < max)
      out[n] = psync_tree_element(cur, int_node, node)->key;
    n++;
  }
  return n;
}

/* Allocate a node and add it to the tree */
static void add_node(psync_tree **root, int key) {
  int_node *n = malloc(sizeof(*n));
  n->key = key;
  psync_tree_add(root, &n->node, cmp_int_nodes);
}

/* Free all nodes in a tree */
static void free_tree(psync_tree *root) {
  psync_tree *cur, *next;
  /* Walk using safe iteration (post-order) */
  cur = psync_tree_get_first_safe(root);
  while (cur) {
    next = psync_tree_get_next_safe(cur);
    free(psync_tree_element(cur, int_node, node));
    cur = next;
  }
}

/* --- Basic tests --- */

START_TEST(test_init_node) {
  int_node n;
  n.node.left = NULL;
  n.node.right = NULL;
  n.node.parent = NULL;
  n.node.height = 1;
  n.key = 42;
  ck_assert_int_eq(n.key, 42);
  ck_assert_int_eq(psync_tree_height(&n.node), 1);
}
END_TEST

START_TEST(test_add_single) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 10);
  ck_assert_ptr_ne(root, NULL);
  ck_assert_int_eq(psync_tree_element(root, int_node, node)->key, 10);
  ck_assert(verify_avl(root) > 0);
  free_tree(root);
}
END_TEST

START_TEST(test_add_three) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 20);
  add_node(&root, 10);
  add_node(&root, 30);

  int keys[3];
  int n = tree_to_array(root, keys, 3);
  ck_assert_int_eq(n, 3);
  ck_assert_int_eq(keys[0], 10);
  ck_assert_int_eq(keys[1], 20);
  ck_assert_int_eq(keys[2], 30);
  ck_assert(verify_avl(root) > 0);
  free_tree(root);
}
END_TEST

START_TEST(test_add_sorted_10) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  for (int i = 0; i < 10; i++)
    add_node(&root, i);

  int keys[10];
  int n = tree_to_array(root, keys, 10);
  ck_assert_int_eq(n, 10);
  for (int i = 0; i < 10; i++)
    ck_assert_int_eq(keys[i], i);
  ck_assert(verify_avl(root) > 0);
  free_tree(root);
}
END_TEST

START_TEST(test_add_reverse_10) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  for (int i = 9; i >= 0; i--)
    add_node(&root, i);

  int keys[10];
  int n = tree_to_array(root, keys, 10);
  ck_assert_int_eq(n, 10);
  for (int i = 0; i < 10; i++)
    ck_assert_int_eq(keys[i], i);
  ck_assert(verify_avl(root) > 0);
  free_tree(root);
}
END_TEST

START_TEST(test_add_zigzag) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  /* Insert in zigzag order to stress rotations */
  int vals[] = {50, 10, 90, 5, 70, 95, 30, 60, 80};
  for (int i = 0; i < 9; i++)
    add_node(&root, vals[i]);
  ck_assert(verify_avl(root) > 0);

  int keys[9];
  int n = tree_to_array(root, keys, 9);
  ck_assert_int_eq(n, 9);
  /* Should be sorted */
  for (int i = 1; i < n; i++)
    ck_assert(keys[i] > keys[i - 1]);
  free_tree(root);
}
END_TEST

/* --- Traversal tests --- */

START_TEST(test_get_first) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 30);
  add_node(&root, 10);
  add_node(&root, 20);
  psync_tree *first = psync_tree_get_first(root);
  ck_assert_int_eq(psync_tree_element(first, int_node, node)->key, 10);
  free_tree(root);
}
END_TEST

START_TEST(test_get_last) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 10);
  add_node(&root, 30);
  add_node(&root, 20);
  psync_tree *last = psync_tree_get_last(root);
  ck_assert_int_eq(psync_tree_element(last, int_node, node)->key, 30);
  free_tree(root);
}
END_TEST

START_TEST(test_get_next_full) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  for (int i = 0; i < 5; i++)
    add_node(&root, i * 10);

  int expected = 0;
  psync_tree *cur;
  psync_tree_for_each(cur, root) {
    ck_assert_int_eq(psync_tree_element(cur, int_node, node)->key, expected);
    expected += 10;
  }
  ck_assert_int_eq(expected, 50);
  free_tree(root);
}
END_TEST

START_TEST(test_get_prev_full) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  for (int i = 0; i < 5; i++)
    add_node(&root, i * 10);

  psync_tree *cur = psync_tree_get_last(root);
  int expected = 40;
  while (cur) {
    ck_assert_int_eq(psync_tree_element(cur, int_node, node)->key, expected);
    expected -= 10;
    cur = psync_tree_get_prev(cur);
  }
  ck_assert_int_eq(expected, -10);
  free_tree(root);
}
END_TEST

START_TEST(test_empty_traversal) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  ck_assert_ptr_eq(psync_tree_get_first(root), NULL);
  ck_assert_ptr_eq(psync_tree_get_last(root), NULL);
}
END_TEST

/* --- Delete tests --- */

START_TEST(test_del_leaf) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 20);
  add_node(&root, 10);
  add_node(&root, 30);

  /* Delete leaf node 10 */
  psync_tree *first = psync_tree_get_first(root);
  int_node *n = psync_tree_element(first, int_node, node);
  ck_assert_int_eq(n->key, 10);
  psync_tree_del(&root, first);
  free(n);

  int keys[2];
  ck_assert_int_eq(tree_to_array(root, keys, 2), 2);
  ck_assert_int_eq(keys[0], 20);
  ck_assert_int_eq(keys[1], 30);
  ck_assert(verify_avl(root) > 0);
  free_tree(root);
}
END_TEST

START_TEST(test_del_one_child) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 20);
  add_node(&root, 10);
  add_node(&root, 30);
  add_node(&root, 25);

  /* Delete 30 which has one child (25) */
  psync_tree *last = psync_tree_get_last(root);
  int_node *n = psync_tree_element(last, int_node, node);
  ck_assert_int_eq(n->key, 30);
  psync_tree_del(&root, last);
  free(n);

  int keys[3];
  ck_assert_int_eq(tree_to_array(root, keys, 3), 3);
  ck_assert_int_eq(keys[0], 10);
  ck_assert_int_eq(keys[1], 20);
  ck_assert_int_eq(keys[2], 25);
  ck_assert(verify_avl(root) > 0);
  free_tree(root);
}
END_TEST

START_TEST(test_del_two_children) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 20);
  add_node(&root, 10);
  add_node(&root, 30);
  add_node(&root, 25);
  add_node(&root, 35);

  /* Find and delete 30 (has two children: 25, 35) */
  psync_tree *cur;
  int_node *target = NULL;
  psync_tree_for_each(cur, root) {
    int_node *n = psync_tree_element(cur, int_node, node);
    if (n->key == 30) {
      target = n;
      break;
    }
  }
  ck_assert_ptr_ne(target, NULL);
  psync_tree_del(&root, &target->node);
  free(target);

  int keys[4];
  ck_assert_int_eq(tree_to_array(root, keys, 4), 4);
  ck_assert_int_eq(keys[0], 10);
  ck_assert_int_eq(keys[1], 20);
  ck_assert_int_eq(keys[2], 25);
  ck_assert_int_eq(keys[3], 35);
  ck_assert(verify_avl(root) > 0);
  free_tree(root);
}
END_TEST

START_TEST(test_del_root_single) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 42);
  int_node *n = psync_tree_element(root, int_node, node);
  psync_tree_del(&root, root);
  free(n);
  ck_assert(psync_tree_isempty(root));
}
END_TEST

START_TEST(test_del_all) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  for (int i = 0; i < 10; i++)
    add_node(&root, i);

  /* Delete all by always removing the first element */
  while (!psync_tree_isempty(root)) {
    psync_tree *first = psync_tree_get_first(root);
    int_node *n = psync_tree_element(first, int_node, node);
    psync_tree_del(&root, first);
    free(n);
    if (!psync_tree_isempty(root))
      ck_assert(verify_avl(root) > 0);
  }
  ck_assert(psync_tree_isempty(root));
}
END_TEST

START_TEST(test_del_rebalance) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  for (int i = 0; i < 20; i++)
    add_node(&root, i);

  /* Delete every other element */
  for (int i = 0; i < 20; i += 2) {
    psync_tree *cur;
    psync_tree_for_each(cur, root) {
      int_node *n = psync_tree_element(cur, int_node, node);
      if (n->key == i) {
        psync_tree_del(&root, cur);
        free(n);
        break;
      }
    }
    if (!psync_tree_isempty(root))
      ck_assert(verify_avl(root) > 0);
  }

  int keys[10];
  int cnt = tree_to_array(root, keys, 10);
  ck_assert_int_eq(cnt, 10);
  for (int i = 0; i < 10; i++)
    ck_assert_int_eq(keys[i], i * 2 + 1);
  free_tree(root);
}
END_TEST

/* --- Positional insertion tests --- */

START_TEST(test_add_after) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 10);
  add_node(&root, 30);

  /* Insert 20 after the node with key 10 */
  psync_tree *first = psync_tree_get_first(root);
  int_node *n = malloc(sizeof(*n));
  n->key = 20;
  psync_tree_add_after(&root, first, &n->node);

  int keys[3];
  ck_assert_int_eq(tree_to_array(root, keys, 3), 3);
  ck_assert_int_eq(keys[0], 10);
  ck_assert_int_eq(keys[1], 20);
  ck_assert_int_eq(keys[2], 30);
  ck_assert(verify_avl(root) > 0);
  free_tree(root);
}
END_TEST

START_TEST(test_add_before) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  add_node(&root, 10);
  add_node(&root, 30);

  /* Insert 20 before the node with key 30 */
  psync_tree *last = psync_tree_get_last(root);
  int_node *n = malloc(sizeof(*n));
  n->key = 20;
  psync_tree_add_before(&root, last, &n->node);

  int keys[3];
  ck_assert_int_eq(tree_to_array(root, keys, 3), 3);
  ck_assert_int_eq(keys[0], 10);
  ck_assert_int_eq(keys[1], 20);
  ck_assert_int_eq(keys[2], 30);
  ck_assert(verify_avl(root) > 0);
  free_tree(root);
}
END_TEST

START_TEST(test_height_check) {
  psync_tree *root = PSYNC_TREE_EMPTY;
  ck_assert_int_eq(psync_tree_height(root), 0);

  /* Build a tree of 100 elements, height should be at most ~7 (log2(100)+1) */
  for (int i = 0; i < 100; i++)
    add_node(&root, i);

  long h = verify_avl(root);
  ck_assert(h > 0);
  ck_assert(h <= 8); /* AVL guarantees h <= 1.44*log2(n+2) */
  free_tree(root);
}
END_TEST

/* --- Suite --- */

static Suite *ptree_suite(void) {
  Suite *s = suite_create("ptree");

  TCase *tc_basic = tcase_create("basic");
  tcase_add_test(tc_basic, test_init_node);
  tcase_add_test(tc_basic, test_add_single);
  tcase_add_test(tc_basic, test_add_three);
  tcase_add_test(tc_basic, test_add_sorted_10);
  tcase_add_test(tc_basic, test_add_reverse_10);
  tcase_add_test(tc_basic, test_add_zigzag);
  suite_add_tcase(s, tc_basic);

  TCase *tc_traversal = tcase_create("traversal");
  tcase_add_test(tc_traversal, test_get_first);
  tcase_add_test(tc_traversal, test_get_last);
  tcase_add_test(tc_traversal, test_get_next_full);
  tcase_add_test(tc_traversal, test_get_prev_full);
  tcase_add_test(tc_traversal, test_empty_traversal);
  suite_add_tcase(s, tc_traversal);

  TCase *tc_delete = tcase_create("delete");
  tcase_add_test(tc_delete, test_del_leaf);
  tcase_add_test(tc_delete, test_del_one_child);
  tcase_add_test(tc_delete, test_del_two_children);
  tcase_add_test(tc_delete, test_del_root_single);
  tcase_add_test(tc_delete, test_del_all);
  tcase_add_test(tc_delete, test_del_rebalance);
  suite_add_tcase(s, tc_delete);

  TCase *tc_pos = tcase_create("positional");
  tcase_add_test(tc_pos, test_add_after);
  tcase_add_test(tc_pos, test_add_before);
  tcase_add_test(tc_pos, test_height_check);
  suite_add_tcase(s, tc_pos);

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
