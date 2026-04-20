/* Unit tests for prunratelimit.c — psync_run_ratelimited and
 * psync_run_debounced semantics.
 *
 * Pattern mirrors check_psyncer.c: pre-define include guards for the heavy
 * headers and provide minimal stubs directly in this TU, then #include the
 * module under test so it links against our fakes.
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>

/* --- Core macros and types --- */

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define D_NONE     0
#define D_BUG      10
#define D_CRITICAL 20
#define D_ERROR    30
#define D_WARNING  40
#define D_NOTICE   50
#define debug(level, ...) do {} while (0)
#define assertw(cond) do {} while (0)

static inline void *psync_malloc(size_t s) { return malloc(s); }
static inline void psync_free(void *p) { free(p); }
#define psync_new(type) ((type *)psync_malloc(sizeof(type)))

/* --- Timer fake ---
 * Small fixed-size pool of virtual timers. Each records its callback,
 * param, and absolute due time. advance_time_to() fires any whose due
 * time has been reached, in order.
 */

typedef struct fake_timer {
  int used;
  int id;
  void (*cb)(struct fake_timer *, void *);
  void *param;
  time_t due;
} fake_timer;

#define FAKE_TIMER_MAX 16
static fake_timer fake_timers[FAKE_TIMER_MAX];
static int fake_timer_next_id;

typedef fake_timer *psync_timer_t;
#define PSYNC_INVALID_TIMER NULL
typedef void (*psync_timer_callback)(struct fake_timer *, void *);

volatile time_t psync_current_time;

static int timer_register_count;
static int timer_stop_count;

static psync_timer_t psync_timer_register(psync_timer_callback func, time_t numsec, void *param) {
  int i;
  for (i = 0; i < FAKE_TIMER_MAX; i++) {
    if (!fake_timers[i].used) {
      fake_timers[i].used = 1;
      fake_timers[i].id = ++fake_timer_next_id;
      fake_timers[i].cb = func;
      fake_timers[i].param = param;
      fake_timers[i].due = psync_current_time + numsec;
      timer_register_count++;
      return &fake_timers[i];
    }
  }
  ck_abort_msg("fake timer pool exhausted");
  return NULL;
}

static int psync_timer_stop(psync_timer_t t) {
  if (!t)
    return 0;
  t->used = 0;
  t->cb = NULL;
  t->param = NULL;
  timer_stop_count++;
  return 0;
}

/* --- Thread fake: run synchronously, recording the call --- */

static int run_thread_count;
static const char *last_run_thread_name;
static void (*last_run_thread_call)(void);
static int run_thread_inline_execute = 1; /* if 0, capture without calling */

static void fake_run_thread(const char *name, void (*call)(void)) {
  run_thread_count++;
  last_run_thread_name = name;
  last_run_thread_call = call;
  if (run_thread_inline_execute && call)
    call();
}
#define psync_run_thread(name, call) fake_run_thread((name), (call))

/* --- Block real headers and stand in for what we need --- */

#define _PSYNC_COMPILER_H
#define PSYNC_MALLOC
#define PSYNC_NOINLINE
#define PSYNC_PURE
#define PSYNC_CONST
#define PSYNC_COLD
#define PSYNC_FORMAT(type, fmt_idx, first_arg) /* empty */
#define PSYNC_NONNULL(...)
#define PSYNC_PACKED_STRUCT
#define PSYNC_THREAD __thread

#define _PSYNC_COMPAT_H
#define _PSYNC_CORE_H
#define _PSYNC_TIMER_H
#define _PSYNC_LIBS_H
#define _PSYNC_MEMALLOC_H

/* Include the real ptree.h (only needs pcompiler.h, which we stubbed) */
#include "../ptree.h"

/* Now pull in prunratelimit.c directly */
#include "../prunratelimit.c"

/* --- Test helpers --- */

static int probe_a_calls;
static int probe_b_calls;
static void probe_a(void) { probe_a_calls++; }
static void probe_b(void) { probe_b_calls++; }

static int rl_calls;
static void rl_cb(void) { rl_calls++; }

static void reset_all(void) {
  int i;
  for (i = 0; i < FAKE_TIMER_MAX; i++)
    fake_timers[i].used = 0;
  fake_timer_next_id = 0;
  timer_register_count = 0;
  timer_stop_count = 0;
  run_thread_count = 0;
  run_thread_inline_execute = 1;
  last_run_thread_name = NULL;
  last_run_thread_call = NULL;
  probe_a_calls = 0;
  probe_b_calls = 0;
  rl_calls = 0;
  psync_current_time = 1000; /* arbitrary non-zero epoch */
  tasks = PSYNC_TREE_EMPTY;
  debounce_tasks = PSYNC_TREE_EMPTY;
}

/* Advance clock one second at a time, firing any due timer.
 * Re-scans the pool after each fire since callbacks can register new timers. */
static void advance_time_to(time_t target) {
  while (psync_current_time < target) {
    psync_current_time++;
    int fired;
    do {
      fired = 0;
      int i;
      for (i = 0; i < FAKE_TIMER_MAX; i++) {
        if (fake_timers[i].used && fake_timers[i].due <= psync_current_time) {
          fake_timer *t = &fake_timers[i];
          psync_timer_callback cb = t->cb;
          void *param = t->param;
          /* Invoke: the real callback will call psync_timer_stop which
           * marks t->used=0. If it re-registers via psync_timer_register,
           * it returns a new slot. */
          cb(t, param);
          fired = 1;
          break;
        }
      }
    } while (fired);
  }
}

static int count_live_timers(void) {
  int i, n = 0;
  for (i = 0; i < FAKE_TIMER_MAX; i++)
    if (fake_timers[i].used) n++;
  return n;
}

static int debounce_tree_count(void) {
  int n = 0;
  psync_tree *t = psync_tree_get_first(debounce_tasks);
  while (t) {
    n++;
    t = psync_tree_get_next(t);
  }
  return n;
}

static int ratelimit_tree_count(void) {
  int n = 0;
  psync_tree *t = psync_tree_get_first(tasks);
  while (t) {
    n++;
    t = psync_tree_get_next(t);
  }
  return n;
}

/* --- Debounce tests --- */

START_TEST(test_leading_fire_on_cold_submit) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  ck_assert_int_eq(debounce_tree_count(), 1);
  ck_assert_int_eq(count_live_timers(), 1);
  /* Test exits before the timer fires; cancel to avoid leaking the node. */
  psync_run_debounce_cancel(probe_a);
}
END_TEST

START_TEST(test_no_refire_within_grace) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  int regs_before = timer_register_count;
  advance_time_to(psync_current_time + 1);
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  ck_assert_int_eq(timer_register_count, regs_before); /* no new timer armed */
  psync_run_debounce_cancel(probe_a);
}
END_TEST

START_TEST(test_trailing_fire_after_quiet) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  psync_run_debounced("probe_a", probe_a, 2, 15, 0); /* pending=1 */
  ck_assert_int_eq(probe_a_calls, 1);
  /* Go quiet for longer than grace. Trailing fire should happen. */
  advance_time_to(psync_current_time + 5);
  ck_assert_int_eq(probe_a_calls, 2);
  ck_assert_int_eq(debounce_tree_count(), 0);
  ck_assert_int_eq(count_live_timers(), 0);
}
END_TEST

START_TEST(test_quiet_after_leading_only_tears_down_without_fire) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  /* No subsequent submits → pending stays 0 → grace timer fires → teardown, no call. */
  advance_time_to(psync_current_time + 5);
  ck_assert_int_eq(probe_a_calls, 1);
  ck_assert_int_eq(debounce_tree_count(), 0);
  ck_assert_int_eq(count_live_timers(), 0);
}
END_TEST

START_TEST(test_ceiling_fire_on_sustained_burst) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1); /* leading */
  time_t start = psync_current_time;
  /* Submit every second for ceilingsec+1 seconds. */
  while (psync_current_time < start + 16) {
    advance_time_to(psync_current_time + 1);
    psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  }
  /* Should have fired leading at t=0 and ceiling at t=15.
   * probe_a_calls should be at least 2. */
  ck_assert_int_ge(probe_a_calls, 2);
  /* The node should still be alive — burst continues. */
  ck_assert_int_eq(debounce_tree_count(), 1);
  ck_assert_int_eq(count_live_timers(), 1);
  psync_run_debounce_cancel(probe_a);
}
END_TEST

START_TEST(test_ceiling_then_trailing_after_quiet) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0); /* leading at t=0 */
  time_t start = psync_current_time;
  while (psync_current_time < start + 16) {
    advance_time_to(psync_current_time + 1);
    psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  }
  int before = probe_a_calls;
  /* Burst ends. Quiet for > grace → trailing fire. */
  advance_time_to(psync_current_time + 5);
  ck_assert_int_eq(probe_a_calls, before + 1);
  ck_assert_int_eq(debounce_tree_count(), 0);
  ck_assert_int_eq(count_live_timers(), 0);
  psync_run_debounce_cancel(probe_a);
}
END_TEST

START_TEST(test_cold_restart_after_trailing) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  advance_time_to(psync_current_time + 5);
  ck_assert_int_eq(probe_a_calls, 2); /* leading + trailing */
  ck_assert_int_eq(debounce_tree_count(), 0);
  /* Fresh submit must fire leading again. */
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 3);
  ck_assert_int_eq(debounce_tree_count(), 1);
  psync_run_debounce_cancel(probe_a);
}
END_TEST

START_TEST(test_grace_extends_on_each_submit) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 3, 15, 0); /* grace=3 */
  ck_assert_int_eq(probe_a_calls, 1);
  int i;
  for (i = 0; i < 5; i++) {
    advance_time_to(psync_current_time + 2); /* always less than grace */
    psync_run_debounced("probe_a", probe_a, 3, 15, 0);
  }
  /* Ceiling is 15, so by t=10 no ceiling yet. Grace should keep resetting.
   * Only leading fired; no trailing yet, no ceiling yet. */
  ck_assert_int_eq(probe_a_calls, 1);
  /* Now go quiet > grace → trailing. */
  advance_time_to(psync_current_time + 4);
  ck_assert_int_eq(probe_a_calls, 2);
  psync_run_debounce_cancel(probe_a);
}
END_TEST

START_TEST(test_two_distinct_callbacks_are_independent) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  psync_run_debounced("probe_b", probe_b, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  ck_assert_int_eq(probe_b_calls, 1);
  ck_assert_int_eq(debounce_tree_count(), 2);
  /* Let probe_a's node go quiet so it tears down, probe_b still active. */
  psync_run_debounced("probe_b", probe_b, 2, 15, 0); /* pending on b */
  advance_time_to(psync_current_time + 5);
  ck_assert_int_eq(probe_a_calls, 1); /* torn down without fire */
  ck_assert_int_eq(probe_b_calls, 2); /* trailing fire */
  ck_assert_int_eq(debounce_tree_count(), 0);
  psync_run_debounce_cancel(probe_a);
  psync_run_debounce_cancel(probe_b);
}
END_TEST

START_TEST(test_grace_zero_coerced_to_one) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 0, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  /* Pending submit, then quiet: trailing fires within 1s since grace coerced. */
  psync_run_debounced("probe_a", probe_a, 0, 15, 0);
  advance_time_to(psync_current_time + 2);
  ck_assert_int_eq(probe_a_calls, 2);
  psync_run_debounce_cancel(probe_a);
}
END_TEST

START_TEST(test_ceiling_less_than_grace_coerced) {
  reset_all();
  /* ceiling=1 < grace=5 → ceiling coerced to grace. */
  psync_run_debounced("probe_a", probe_a, 5, 1, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  /* Keep submitting. Since ceiling==grace after coercion, each wakes up
   * at the grace/ceiling deadline. */
  int i;
  for (i = 0; i < 10; i++) {
    advance_time_to(psync_current_time + 1);
    psync_run_debounced("probe_a", probe_a, 5, 1, 0);
  }
  /* Burst quieter than grace: 1s submits, grace=5 → no ceiling fires since
   * ceiling was coerced to 5 and submit keeps pushing last_submit. */
  advance_time_to(psync_current_time + 10);
  /* After quiet, trailing fires. */
  ck_assert_int_ge(probe_a_calls, 2);
}
END_TEST

START_TEST(test_debounce_cancel_happy_path) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1); /* leading fired */
  ck_assert_int_eq(debounce_tree_count(), 1);
  ck_assert_int_eq(count_live_timers(), 1);
  /* Cancel immediately, before the timer fires. */
  int rc = psync_run_debounce_cancel(probe_a);
  ck_assert_int_eq(rc, 1);
  ck_assert_int_eq(debounce_tree_count(), 0);
  ck_assert_int_eq(count_live_timers(), 0); /* timer reaped */
  /* Wait past grace; no trailing fire should happen because there is no node. */
  advance_time_to(psync_current_time + 10);
  ck_assert_int_eq(probe_a_calls, 1);
}
END_TEST

START_TEST(test_debounce_cancel_noop_when_none_registered) {
  reset_all();
  int rc = psync_run_debounce_cancel(probe_a);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(debounce_tree_count(), 0);
  ck_assert_int_eq(count_live_timers(), 0);
}
END_TEST

START_TEST(test_debounce_cancel_with_pending_suppresses_trailing) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0); /* leading */
  ck_assert_int_eq(probe_a_calls, 1);
  /* Second submit sets pending=1 → would normally produce a trailing fire. */
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  /* Cancel before the trailing fire. */
  int rc = psync_run_debounce_cancel(probe_a);
  ck_assert_int_eq(rc, 1);
  ck_assert_int_eq(debounce_tree_count(), 0);
  ck_assert_int_eq(count_live_timers(), 0);
  /* Wait well past gracesec — trailing must NOT fire. */
  advance_time_to(psync_current_time + 20);
  ck_assert_int_eq(probe_a_calls, 1);
}
END_TEST

START_TEST(test_debounce_cancel_then_register_fresh) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0); /* leading */
  ck_assert_int_eq(probe_a_calls, 1);
  int rc = psync_run_debounce_cancel(probe_a);
  ck_assert_int_eq(rc, 1);
  ck_assert_int_eq(debounce_tree_count(), 0);
  /* Re-register: must act as a fresh leading edge (probe_a fires again). */
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  ck_assert_int_eq(probe_a_calls, 2);
  ck_assert_int_eq(debounce_tree_count(), 1);
  ck_assert_int_eq(count_live_timers(), 1);
  /* Clean up so the test exits without leaks. */
  rc = psync_run_debounce_cancel(probe_a);
  ck_assert_int_eq(rc, 1);
}
END_TEST

START_TEST(test_debounce_cancel_only_targets_matching_callback) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  psync_run_debounced("probe_b", probe_b, 2, 15, 0);
  ck_assert_int_eq(debounce_tree_count(), 2);
  /* Cancel probe_a only. probe_b's node and timer must remain. */
  int rc = psync_run_debounce_cancel(probe_a);
  ck_assert_int_eq(rc, 1);
  ck_assert_int_eq(debounce_tree_count(), 1);
  ck_assert_int_eq(count_live_timers(), 1);
  /* Drain probe_b naturally to keep the test leak-free. */
  advance_time_to(psync_current_time + 10);
  ck_assert_int_eq(debounce_tree_count(), 0);
  ck_assert_int_eq(count_live_timers(), 0);
}
END_TEST

/* --- Ratelimit coexistence tests --- */

START_TEST(test_ratelimit_leading_fire_and_no_refire) {
  reset_all();
  psync_run_ratelimited("rl_cb", rl_cb, 10, 0);
  ck_assert_int_eq(rl_calls, 1);
  psync_run_ratelimited("rl_cb", rl_cb, 10, 0);
  ck_assert_int_eq(rl_calls, 1); /* coalesced */
  ck_assert_int_eq(ratelimit_tree_count(), 1);
  /* Drain the cooldown so the node tears down naturally; otherwise the
   * test exits with a live node and an armed timer (LeakSanitizer-visible). */
  advance_time_to(psync_current_time + 20);
  ck_assert_int_eq(ratelimit_tree_count(), 0);
}
END_TEST

START_TEST(test_ratelimit_trailing_fire) {
  reset_all();
  psync_run_ratelimited("rl_cb", rl_cb, 10, 0);
  psync_run_ratelimited("rl_cb", rl_cb, 10, 0); /* scheduled=1 */
  advance_time_to(psync_current_time + 11);
  ck_assert_int_eq(rl_calls, 2);
}
END_TEST

START_TEST(test_debounce_and_ratelimit_coexist) {
  reset_all();
  psync_run_debounced("probe_a", probe_a, 2, 15, 0);
  psync_run_ratelimited("rl_cb", rl_cb, 10, 0);
  ck_assert_int_eq(probe_a_calls, 1);
  ck_assert_int_eq(rl_calls, 1);
  ck_assert_int_eq(debounce_tree_count(), 1);
  ck_assert_int_eq(ratelimit_tree_count(), 1);
  /* Quiet → both tear down. Debounce with no pending fires nothing more;
   * ratelimit with no scheduled fires nothing more. */
  advance_time_to(psync_current_time + 20);
  ck_assert_int_eq(probe_a_calls, 1);
  ck_assert_int_eq(rl_calls, 1);
  ck_assert_int_eq(debounce_tree_count(), 0);
  ck_assert_int_eq(ratelimit_tree_count(), 0);
}
END_TEST

/* --- Suite --- */

static Suite *prunratelimit_suite(void) {
  Suite *s = suite_create("prunratelimit");

  TCase *tc_deb = tcase_create("debounce");
  tcase_add_test(tc_deb, test_leading_fire_on_cold_submit);
  tcase_add_test(tc_deb, test_no_refire_within_grace);
  tcase_add_test(tc_deb, test_trailing_fire_after_quiet);
  tcase_add_test(tc_deb, test_quiet_after_leading_only_tears_down_without_fire);
  tcase_add_test(tc_deb, test_ceiling_fire_on_sustained_burst);
  tcase_add_test(tc_deb, test_ceiling_then_trailing_after_quiet);
  tcase_add_test(tc_deb, test_cold_restart_after_trailing);
  tcase_add_test(tc_deb, test_grace_extends_on_each_submit);
  tcase_add_test(tc_deb, test_two_distinct_callbacks_are_independent);
  tcase_add_test(tc_deb, test_grace_zero_coerced_to_one);
  tcase_add_test(tc_deb, test_ceiling_less_than_grace_coerced);
  tcase_add_test(tc_deb, test_debounce_cancel_happy_path);
  tcase_add_test(tc_deb, test_debounce_cancel_noop_when_none_registered);
  tcase_add_test(tc_deb, test_debounce_cancel_with_pending_suppresses_trailing);
  tcase_add_test(tc_deb, test_debounce_cancel_then_register_fresh);
  tcase_add_test(tc_deb, test_debounce_cancel_only_targets_matching_callback);
  suite_add_tcase(s, tc_deb);

  TCase *tc_rl = tcase_create("ratelimit");
  tcase_add_test(tc_rl, test_ratelimit_leading_fire_and_no_refire);
  tcase_add_test(tc_rl, test_ratelimit_trailing_fire);
  tcase_add_test(tc_rl, test_debounce_and_ratelimit_coexist);
  suite_add_tcase(s, tc_rl);

  return s;
}

int main(void) {
  Suite *s = prunratelimit_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
