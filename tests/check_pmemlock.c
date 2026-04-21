#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "fff.h"
DEFINE_FFF_GLOBALS;

#include "pmemlock.h"
#include "pintervaltree.h"

/* --- FFF fakes for pcompat memory functions --- */

FAKE_VALUE_FUNC(void *, psync_mmap_anon_safe, size_t);
FAKE_VALUE_FUNC(int, psync_munmap_anon, void *, size_t);
FAKE_VALUE_FUNC(int, psync_mlock, void *, size_t);
FAKE_VALUE_FUNC(int, psync_munlock, void *, size_t);
FAKE_VALUE_FUNC(int, psync_get_page_size);
FAKE_VOID_FUNC(psync_cloud_crypto_clean_cache);

/* --- Stub implementations for unit tests --- */

static void *stub_mmap(size_t size) {
  return calloc(1, size);
}

static int stub_munmap(void *ptr, size_t size) {
  (void)size;
  free(ptr);
  return 0;
}

/* --- Real implementations for integration tests --- */

static void *real_mmap(size_t size) {
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return (p == MAP_FAILED) ? NULL : p;
}

static int real_munmap(void *ptr, size_t size) {
  return munmap(ptr, size);
}

static int real_mlock_fn(void *ptr, size_t size) {
  return mlock(ptr, size);
}

static int real_munlock_fn(void *ptr, size_t size) {
  return munlock(ptr, size);
}

static int real_page_size(void) {
  return (int)sysconf(_SC_PAGESIZE);
}

/* --- Constants matching pmemlock.c internals --- */

#define LM_ALIGN_TO  (sizeof(size_t) * 2)
#define LM_OVERHEAD  (sizeof(size_t) * 2)
#define LM_END_MARKER (~((size_t)0))

/* ================================================================
 *  Unit tests — page locking
 * ================================================================ */

static void page_setup(void) {
  RESET_FAKE(psync_mmap_anon_safe);
  RESET_FAKE(psync_munmap_anon);
  RESET_FAKE(psync_mlock);
  RESET_FAKE(psync_munlock);
  RESET_FAKE(psync_get_page_size);
  RESET_FAKE(psync_cloud_crypto_clean_cache);
  FFF_RESET_HISTORY();
  psync_get_page_size_fake.return_val = 4096;
  psync_mlock_fake.return_val = 0;
  psync_munlock_fake.return_val = 0;
}

START_TEST(test_lock_single_page) {
  char buf[16];
  ck_assert_int_eq(psync_mem_lock(buf, sizeof(buf)), 0);
  ck_assert_uint_eq(psync_mlock_fake.call_count, 1);
  ck_assert_int_eq(psync_mem_unlock(buf, sizeof(buf)), 0);
  ck_assert_uint_eq(psync_munlock_fake.call_count, 1);
}
END_TEST

START_TEST(test_lock_refcount) {
  char buf[16];
  ck_assert_int_eq(psync_mem_lock(buf, sizeof(buf)), 0);
  ck_assert_int_eq(psync_mem_lock(buf, sizeof(buf)), 0);
  /* mlock called once — second lock increments refcount */
  ck_assert_uint_eq(psync_mlock_fake.call_count, 1);
  /* First unlock decrements refcount, no munlock yet */
  ck_assert_int_eq(psync_mem_unlock(buf, sizeof(buf)), 0);
  ck_assert_uint_eq(psync_munlock_fake.call_count, 0);
  /* Second unlock → refcount hits 0, munlock called */
  ck_assert_int_eq(psync_mem_unlock(buf, sizeof(buf)), 0);
  ck_assert_uint_eq(psync_munlock_fake.call_count, 1);
}
END_TEST

START_TEST(test_lock_multi_page_span) {
  /* Create a region that spans exactly 3 pages by using pointer arithmetic.
   * We use page_size=4096 and a 3-page region. */
  int page_size = 4096;
  /* Allocate a buffer that spans 3 pages. Use a large stack buffer. */
  size_t region_size = page_size * 3 - 1;
  void *buf = malloc(region_size);
  /* Align buf to a page boundary for predictable page counting */
  uintptr_t aligned = ((uintptr_t)buf + page_size - 1) / page_size * page_size;
  /* Region: from aligned, size = 2*page_size + 1 → spans 3 pages */
  size_t span_size = 2 * page_size + 1;
  if (aligned + span_size <= (uintptr_t)buf + region_size) {
    ck_assert_int_eq(psync_mem_lock((void *)aligned, span_size), 0);
    ck_assert_uint_eq(psync_mlock_fake.call_count, 3);
    ck_assert_int_eq(psync_mem_unlock((void *)aligned, span_size), 0);
    ck_assert_uint_eq(psync_munlock_fake.call_count, 3);
  }
  free(buf);
}
END_TEST

START_TEST(test_unlock_not_locked) {
  char buf[16];
  int ret = psync_mem_unlock(buf, sizeof(buf));
  ck_assert_int_eq(ret, -1);
  ck_assert_uint_eq(psync_munlock_fake.call_count, 0);
}
END_TEST

START_TEST(test_lock_rollback_on_failure) {
  /* Region spanning 3 pages. mlock fails on 3rd page.
   * lock_page retries once after clean_cache, so sequence:
   * page1: mlock OK (0), page2: mlock OK (0),
   * page3: mlock FAIL (-1), clean_cache, mlock FAIL (-1) again.
   * Then rollback: munlock page2, munlock page1. */
  int page_size = 4096;
  size_t region_size = page_size * 4;
  void *buf = malloc(region_size);
  uintptr_t aligned = ((uintptr_t)buf + page_size - 1) / page_size * page_size;
  size_t span_size = 2 * page_size + 1;

  if (aligned + span_size <= (uintptr_t)buf + region_size) {
    int mlock_returns[] = {0, 0, -1, -1};
    SET_RETURN_SEQ(psync_mlock, mlock_returns, 4);

    int ret = psync_mem_lock((void *)aligned, span_size);
    ck_assert_int_eq(ret, -1);
    ck_assert_uint_eq(psync_mlock_fake.call_count, 4);
    ck_assert_uint_eq(psync_cloud_crypto_clean_cache_fake.call_count, 1);
    /* Rollback unlocks the 2 successfully locked pages */
    ck_assert_uint_eq(psync_munlock_fake.call_count, 2);
  }
  free(buf);
}
END_TEST

/* ================================================================
 *  Unit tests — locked allocator
 * ================================================================ */

static void alloc_setup(void) {
  RESET_FAKE(psync_mmap_anon_safe);
  RESET_FAKE(psync_munmap_anon);
  RESET_FAKE(psync_mlock);
  RESET_FAKE(psync_munlock);
  RESET_FAKE(psync_get_page_size);
  RESET_FAKE(psync_cloud_crypto_clean_cache);
  FFF_RESET_HISTORY();
  psync_mmap_anon_safe_fake.custom_fake = stub_mmap;
  psync_munmap_anon_fake.custom_fake = stub_munmap;
  psync_mlock_fake.return_val = 0;
  psync_munlock_fake.return_val = 0;
  psync_get_page_size_fake.return_val = 4096;
  psync_locked_init();
}

START_TEST(test_init) {
  /* psync_locked_init called in setup — just verify no crash */
  psync_locked_init();
}
END_TEST

START_TEST(test_malloc_free_single) {
  void *p = psync_locked_malloc(64);
  ck_assert_ptr_ne(p, NULL);
  memset(p, 0xAB, 64);
  psync_locked_free(p);
}
END_TEST

START_TEST(test_malloc_multiple_from_same_range) {
  void *p1 = psync_locked_malloc(32);
  void *p2 = psync_locked_malloc(32);
  ck_assert_ptr_ne(p1, NULL);
  ck_assert_ptr_ne(p2, NULL);
  ck_assert_ptr_ne(p1, p2);
  /* Both should come from the same mmap'd range → only 1 mmap call */
  ck_assert_uint_eq(psync_mmap_anon_safe_fake.call_count, 1);
  psync_locked_free(p1);
  psync_locked_free(p2);
}
END_TEST

START_TEST(test_malloc_free_reuse) {
  /* Keep an anchor allocation alive so the range isn't cleaned up on free */
  void *anchor = psync_locked_malloc(16);
  ck_assert_ptr_ne(anchor, NULL);
  void *p1 = psync_locked_malloc(64);
  ck_assert_ptr_ne(p1, NULL);
  psync_locked_free(p1);
  /* Freed interval should be reused — no new mmap needed */
  unsigned int mmap_calls_before = psync_mmap_anon_safe_fake.call_count;
  void *p2 = psync_locked_malloc(64);
  ck_assert_ptr_ne(p2, NULL);
  ck_assert_uint_eq(psync_mmap_anon_safe_fake.call_count, mmap_calls_before);
  psync_locked_free(p2);
  psync_locked_free(anchor);
}
END_TEST

START_TEST(test_malloc_end_marker) {
  size_t req_size = 32;
  void *p = psync_locked_malloc(req_size);
  ck_assert_ptr_ne(p, NULL);
  /* Compute end marker position from request size (works in both debug/release).
   * alloc_size = align(req_size) + LM_OVERHEAD. End marker is at
   * cptr + alloc_size - sizeof(size_t), where cptr = p - sizeof(size_t). */
  size_t alloc_size = ((req_size + LM_ALIGN_TO - 1) / LM_ALIGN_TO * LM_ALIGN_TO) + LM_OVERHEAD;
  size_t *end = (size_t *)((char *)p - sizeof(size_t) + alloc_size - sizeof(size_t));
  ck_assert(*end == LM_END_MARKER);
  psync_locked_free(p);
}
END_TEST

START_TEST(test_malloc_alignment) {
  void *p = psync_locked_malloc(1);
  ck_assert_ptr_ne(p, NULL);
  ck_assert_uint_eq((uintptr_t)p % LM_ALIGN_TO, 0);
  psync_locked_free(p);

  void *p2 = psync_locked_malloc(7);
  ck_assert_ptr_ne(p2, NULL);
  ck_assert_uint_eq((uintptr_t)p2 % LM_ALIGN_TO, 0);
  psync_locked_free(p2);
}
END_TEST

START_TEST(test_free_full_range_cleanup) {
  /* Allocate a single item → creates one range.
   * Free it → range should be fully freed (munmap called).
   * This verifies the interval tree leak fix. */
  void *p = psync_locked_malloc(64);
  ck_assert_ptr_ne(p, NULL);
  unsigned int munmap_before = psync_munmap_anon_fake.call_count;
  psync_locked_free(p);
  /* Range should have been cleaned up → munmap called */
  ck_assert_uint_eq(psync_munmap_anon_fake.call_count, munmap_before + 1);
}
END_TEST

START_TEST(test_malloc_mlock_failure) {
  /* mlock always fails → allocator should still succeed, just unlocked */
  psync_mlock_fake.return_val = -1;
  void *p = psync_locked_malloc(64);
  ck_assert_ptr_ne(p, NULL);
  memset(p, 0xCC, 64);
  psync_locked_free(p);
}
END_TEST

START_TEST(test_malloc_overflow_size) {
  void *p = psync_locked_malloc(SIZE_MAX - 1);
  ck_assert_ptr_eq(p, NULL);
  p = psync_locked_malloc(SIZE_MAX);
  ck_assert_ptr_eq(p, NULL);
}
END_TEST

/* ================================================================
 *  Integration tests — real mmap/mlock
 * ================================================================ */

static void integ_setup(void) {
  RESET_FAKE(psync_mmap_anon_safe);
  RESET_FAKE(psync_munmap_anon);
  RESET_FAKE(psync_mlock);
  RESET_FAKE(psync_munlock);
  RESET_FAKE(psync_get_page_size);
  RESET_FAKE(psync_cloud_crypto_clean_cache);
  FFF_RESET_HISTORY();
  psync_mmap_anon_safe_fake.custom_fake = real_mmap;
  psync_munmap_anon_fake.custom_fake = real_munmap;
  psync_mlock_fake.custom_fake = real_mlock_fn;
  psync_munlock_fake.custom_fake = real_munlock_fn;
  psync_get_page_size_fake.custom_fake = real_page_size;
  psync_locked_init();
}

START_TEST(test_integ_alloc_write_free) {
  void *p = psync_locked_malloc(128);
  ck_assert_ptr_ne(p, NULL);
  memset(p, 0xDE, 128);
  /* Verify written data is readable */
  unsigned char *cp = (unsigned char *)p;
  for (int i = 0; i < 128; i++)
    ck_assert_uint_eq(cp[i], 0xDE);
  psync_locked_free(p);
}
END_TEST

START_TEST(test_integ_multiple_allocs) {
  void *ptrs[8];
  for (int i = 0; i < 8; i++) {
    ptrs[i] = psync_locked_malloc(32);
    ck_assert_ptr_ne(ptrs[i], NULL);
    memset(ptrs[i], (unsigned char)i, 32);
  }
  /* Verify each allocation has distinct data */
  for (int i = 0; i < 8; i++) {
    unsigned char *cp = (unsigned char *)ptrs[i];
    ck_assert_uint_eq(cp[0], (unsigned char)i);
  }
  for (int i = 0; i < 8; i++)
    psync_locked_free(ptrs[i]);
}
END_TEST

START_TEST(test_integ_alloc_free_alloc) {
  void *p1 = psync_locked_malloc(64);
  ck_assert_ptr_ne(p1, NULL);
  psync_locked_free(p1);
  void *p2 = psync_locked_malloc(64);
  ck_assert_ptr_ne(p2, NULL);
  memset(p2, 0xAA, 64);
  psync_locked_free(p2);
}
END_TEST

START_TEST(test_integ_page_lock_unlock) {
  /* Use real mmap to get a page-aligned region, then lock/unlock it.
   * mlock may fail due to ulimit — that's OK, we just verify no crash. */
  int ps = (int)sysconf(_SC_PAGESIZE);
  void *region = mmap(NULL, ps, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ck_assert_ptr_ne(region, MAP_FAILED);
  int ret = psync_mem_lock(region, ps);
  if (ret == 0)
    ck_assert_int_eq(psync_mem_unlock(region, ps), 0);
  /* If ret == -1, mlock failed (ulimit) — still no crash */
  munmap(region, ps);
}
END_TEST

START_TEST(test_integ_full_lifecycle) {
  /* init → alloc → write → free → verify cleanup */
  void *p = psync_locked_malloc(256);
  ck_assert_ptr_ne(p, NULL);
  memset(p, 0xFF, 256);
  psync_locked_free(p);
  /* After freeing the only allocation, munmap should have been called */
  ck_assert(psync_munmap_anon_fake.call_count >= 1);
}
END_TEST

/* ================================================================
 *  Suite
 * ================================================================ */

static Suite *pmemlock_suite(void) {
  Suite *s = suite_create("pmemlock");

  TCase *tc_page = tcase_create("page_locking");
  tcase_add_checked_fixture(tc_page, page_setup, NULL);
  tcase_add_test(tc_page, test_lock_single_page);
  tcase_add_test(tc_page, test_lock_refcount);
  tcase_add_test(tc_page, test_lock_multi_page_span);
  tcase_add_test(tc_page, test_unlock_not_locked);
  tcase_add_test(tc_page, test_lock_rollback_on_failure);
  suite_add_tcase(s, tc_page);

  TCase *tc_alloc = tcase_create("locked_allocator");
  tcase_add_checked_fixture(tc_alloc, alloc_setup, NULL);
  tcase_add_test(tc_alloc, test_init);
  tcase_add_test(tc_alloc, test_malloc_free_single);
  tcase_add_test(tc_alloc, test_malloc_multiple_from_same_range);
  tcase_add_test(tc_alloc, test_malloc_free_reuse);
  tcase_add_test(tc_alloc, test_malloc_end_marker);
  tcase_add_test(tc_alloc, test_malloc_alignment);
  tcase_add_test(tc_alloc, test_free_full_range_cleanup);
  tcase_add_test(tc_alloc, test_malloc_mlock_failure);
  tcase_add_test(tc_alloc, test_malloc_overflow_size);
  suite_add_tcase(s, tc_alloc);

  TCase *tc_integ = tcase_create("integration");
  tcase_add_checked_fixture(tc_integ, integ_setup, NULL);
  tcase_add_test(tc_integ, test_integ_alloc_write_free);
  tcase_add_test(tc_integ, test_integ_multiple_allocs);
  tcase_add_test(tc_integ, test_integ_alloc_free_alloc);
  tcase_add_test(tc_integ, test_integ_page_lock_unlock);
  tcase_add_test(tc_integ, test_integ_full_lifecycle);
  suite_add_tcase(s, tc_integ);

  return s;
}

int main(void) {
  Suite *s = pmemlock_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
