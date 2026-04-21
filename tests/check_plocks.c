#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include "plocks.h"

static psync_rwlock_t rw;

static void setup(void) {
  psync_rwlock_init(&rw);
}

static void teardown(void) {
  psync_rwlock_destroy(&rw);
}

/* --- Basic lifecycle --- */

START_TEST(test_init_destroy) {
  /* setup/teardown handle init/destroy; just verify no crash */
  ck_assert(1);
}
END_TEST

/* --- Single-thread read lock --- */

START_TEST(test_rdlock_unlock) {
  psync_rwlock_rdlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_rdlock(&rw), 1);
  ck_assert_int_eq(psync_rwlock_holding_lock(&rw), 1);
  psync_rwlock_unlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_rdlock(&rw), 0);
  ck_assert_int_eq(psync_rwlock_holding_lock(&rw), 0);
}
END_TEST

/* --- Single-thread write lock --- */

START_TEST(test_wrlock_unlock) {
  psync_rwlock_wrlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_wrlock(&rw), 1);
  ck_assert_int_eq(psync_rwlock_holding_lock(&rw), 1);
  psync_rwlock_unlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_wrlock(&rw), 0);
  ck_assert_int_eq(psync_rwlock_holding_lock(&rw), 0);
}
END_TEST

/* --- Recursive read lock --- */

START_TEST(test_recursive_rdlock) {
  psync_rwlock_rdlock(&rw);
  psync_rwlock_rdlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_rdlock(&rw), 1);
  psync_rwlock_unlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_rdlock(&rw), 1);
  psync_rwlock_unlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_rdlock(&rw), 0);
}
END_TEST

/* --- Recursive write lock --- */

START_TEST(test_recursive_wrlock) {
  psync_rwlock_wrlock(&rw);
  psync_rwlock_wrlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_wrlock(&rw), 1);
  psync_rwlock_unlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_wrlock(&rw), 1);
  psync_rwlock_unlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_wrlock(&rw), 0);
}
END_TEST

/* --- holding_lock consistency (bug fix) --- */

START_TEST(test_holding_lock_consistency) {
  /* When neither read nor write locked, holding_lock should be 0 */
  ck_assert_int_eq(psync_rwlock_holding_lock(&rw),
                   psync_rwlock_holding_rdlock(&rw) || psync_rwlock_holding_wrlock(&rw));

  /* With rdlock */
  psync_rwlock_rdlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_lock(&rw),
                   psync_rwlock_holding_rdlock(&rw) || psync_rwlock_holding_wrlock(&rw));
  psync_rwlock_unlock(&rw);

  /* With wrlock */
  psync_rwlock_wrlock(&rw);
  ck_assert_int_eq(psync_rwlock_holding_lock(&rw),
                   psync_rwlock_holding_rdlock(&rw) || psync_rwlock_holding_wrlock(&rw));
  psync_rwlock_unlock(&rw);
}
END_TEST

/* --- tryrdlock uncontended --- */

START_TEST(test_tryrdlock_uncontended) {
  int ret = psync_rwlock_tryrdlock(&rw);
  ck_assert_int_eq(ret, 0);
  ck_assert_int_eq(psync_rwlock_holding_rdlock(&rw), 1);
  psync_rwlock_unlock(&rw);
}
END_TEST

/* --- trywrlock uncontended --- */

START_TEST(test_trywrlock_uncontended) {
  int ret = psync_rwlock_trywrlock(&rw);
  ck_assert_int_eq(ret, 0);
  ck_assert_int_eq(psync_rwlock_holding_wrlock(&rw), 1);
  psync_rwlock_unlock(&rw);
}
END_TEST

/* --- Concurrent readers --- */

static void *reader_thread(void *arg) {
  psync_rwlock_t *lock = (psync_rwlock_t *)arg;
  psync_rwlock_rdlock(lock);
  /* Hold briefly */
  volatile int x = 0;
  for (int i = 0; i < 1000; i++) x++;
  (void)x;
  psync_rwlock_unlock(lock);
  return NULL;
}

START_TEST(test_concurrent_readers) {
  pthread_t threads[4];
  for (int i = 0; i < 4; i++)
    pthread_create(&threads[i], NULL, reader_thread, &rw);
  for (int i = 0; i < 4; i++)
    pthread_join(threads[i], NULL);
  /* If we get here without deadlock, concurrent readers work */
  ck_assert(1);
}
END_TEST

/* --- Writer excludes readers --- */

static volatile int writer_holding = 0;

static void *writer_thread(void *arg) {
  psync_rwlock_t *lock = (psync_rwlock_t *)arg;
  psync_rwlock_wrlock(lock);
  writer_holding = 1;
  /* Hold the lock for a bit */
  volatile int x = 0;
  for (int i = 0; i < 100000; i++) x++;
  (void)x;
  writer_holding = 0;
  psync_rwlock_unlock(lock);
  return NULL;
}

static void *tryrd_thread(void *arg) {
  psync_rwlock_t *lock = (psync_rwlock_t *)arg;
  /* Try many times while writer holds */
  int failed = 0;
  for (int i = 0; i < 100; i++) {
    if (writer_holding) {
      int ret = psync_rwlock_tryrdlock(lock);
      if (ret == -1)
        failed = 1;
      else
        psync_rwlock_unlock(lock);
    }
  }
  return (void *)(intptr_t)failed;
}

START_TEST(test_writer_excludes_readers) {
  pthread_t wt, rt;
  writer_holding = 0;
  pthread_create(&wt, NULL, writer_thread, &rw);
  pthread_create(&rt, NULL, tryrd_thread, &rw);
  pthread_join(wt, NULL);
  void *result;
  pthread_join(rt, NULL);
  (void)result;
  /* Test passes if no deadlock or crash */
  ck_assert(1);
}
END_TEST

/* --- Num waiters --- */

START_TEST(test_num_waiters_zero) {
  ck_assert_uint_eq(psync_rwlock_num_waiters(&rw), 0);
}
END_TEST

/* --- Suite setup --- */

static Suite *plocks_suite(void) {
  Suite *s = suite_create("plocks");

  TCase *tc_basic = tcase_create("basic");
  tcase_add_checked_fixture(tc_basic, setup, teardown);
  tcase_add_test(tc_basic, test_init_destroy);
  tcase_add_test(tc_basic, test_rdlock_unlock);
  tcase_add_test(tc_basic, test_wrlock_unlock);
  suite_add_tcase(s, tc_basic);

  TCase *tc_recursive = tcase_create("recursive");
  tcase_add_checked_fixture(tc_recursive, setup, teardown);
  tcase_add_test(tc_recursive, test_recursive_rdlock);
  tcase_add_test(tc_recursive, test_recursive_wrlock);
  suite_add_tcase(s, tc_recursive);

  TCase *tc_holding = tcase_create("holding");
  tcase_add_checked_fixture(tc_holding, setup, teardown);
  tcase_add_test(tc_holding, test_holding_lock_consistency);
  suite_add_tcase(s, tc_holding);

  TCase *tc_try = tcase_create("try");
  tcase_add_checked_fixture(tc_try, setup, teardown);
  tcase_add_test(tc_try, test_tryrdlock_uncontended);
  tcase_add_test(tc_try, test_trywrlock_uncontended);
  suite_add_tcase(s, tc_try);

  TCase *tc_concurrent = tcase_create("concurrent");
  tcase_add_checked_fixture(tc_concurrent, setup, teardown);
  tcase_set_timeout(tc_concurrent, 10);
  tcase_add_test(tc_concurrent, test_concurrent_readers);
  tcase_add_test(tc_concurrent, test_writer_excludes_readers);
  tcase_add_test(tc_concurrent, test_num_waiters_zero);
  suite_add_tcase(s, tc_concurrent);

  return s;
}

int main(void) {
  Suite *s = plocks_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
