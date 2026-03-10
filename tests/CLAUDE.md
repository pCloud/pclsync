# Tests

## Overview

Unit tests for the pclsync C library. Each test compiles the module under test directly (no library linking) to enable isolated, fast testing.

## Frameworks

- **Check** — C unit testing framework. Provides `START_TEST`/`END_TEST` blocks, `ck_assert_*` macros, suites, and test cases. Docs: https://libcheck.github.io/check/
- **FFF** (`fff.h`) — Fake Function Framework, a header-only C mocking library. Generates fake implementations of functions with call tracking, argument capture, and return value control.

## Running Tests

```sh
make check USESSL=openssl    # from the project root
make check                   # from tests/
```

`USESSL=openssl` is needed only when running from the project root because the root Makefile builds the full library first. The `tests/Makefile` does not use `USESSL` — it compiles modules directly.

Most tests are SSL-backend-agnostic. Tests for a specific SSL backend (e.g., `pssl-openssl.c`, `pssl-mbedtls.c`) will compile against that backend's headers directly and may require the corresponding library to be installed. Do not assume `openssl` is always the right backend — match `USESSL=` to whatever the test under development actually needs.

## File Conventions

### Naming

- Test source files: `check_{module}.c` (e.g., `check_plist.c` tests `plist.c`)
- The Makefile auto-discovers `check_*.c` via wildcard
- Test binaries go to `build/` (git-ignored)

### Shims

`pcompat.h` in this directory shadows the library's `pcompat.h` to provide minimal type stubs (e.g., `psync_uint_t`). This lets modules compile without pulling in the full platform abstraction layer. Add stubs here only when a module under test needs a type or macro from `pcompat.h`.

### Makefile

Each test binary needs an explicit build rule in the Makefile:

```makefile
$(BUILD_DIR)/check_foo: check_foo.c ../foo.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
```

Include all `.c` files the module depends on directly. No library archive is linked — the test compiles the source files it needs.

## Writing Tests with Check

### Structure

Every test file follows this layout:

```c
#include <check.h>
#include <stdlib.h>
#include "module_under_test.h"

/* --- Helpers --- */

/* ... helper functions, comparators, etc. ... */

/* --- Tests --- */

START_TEST(test_descriptive_name) {
  /* arrange */
  /* act */
  /* assert using ck_assert_* */
}
END_TEST

/* --- Suite setup --- */

static Suite *module_suite(void) {
  Suite *s = suite_create("module");

  TCase *tc_group = tcase_create("group_name");
  tcase_add_test(tc_group, test_descriptive_name);
  suite_add_tcase(s, tc_group);

  return s;
}

int main(void) {
  Suite *s = module_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

### Assertions

Use Check's typed assertions — they produce clear failure messages:

| Assertion | Use for |
|-----------|---------|
| `ck_assert(expr)` | Boolean truth |
| `ck_assert_int_eq(a, b)` | Integer equality |
| `ck_assert_int_ne(a, b)` | Integer inequality |
| `ck_assert_uint_eq(a, b)` | Unsigned integer equality |
| `ck_assert_str_eq(a, b)` | String equality |
| `ck_assert_str_ne(a, b)` | String inequality |
| `ck_assert_ptr_eq(a, b)` | Pointer equality (including NULL checks) |
| `ck_assert_ptr_ne(a, b)` | Pointer inequality |
| `ck_assert_mem_eq(a, b, n)` | Memory buffer equality |
| `ck_assert_float_eq_tol(a, b, tol)` | Float equality within tolerance |

Avoid bare `ck_assert(a == b)` when a typed variant exists — the typed form prints both values on failure.

### Test Cases

Group related tests into `TCase` objects by functional area:

```c
TCase *tc_basic = tcase_create("basic");
TCase *tc_edge  = tcase_create("edge_cases");
TCase *tc_error = tcase_create("error_handling");
```

Use `tcase_add_checked_fixture(tc, setup_fn, teardown_fn)` when multiple tests share the same setup/teardown. Prefer per-test local setup for simple cases.

### Naming

- Test functions: `test_{what_is_being_tested}` in snake_case (e.g., `test_list_sort_reversed`)
- Suite function: `{module}_suite`
- Test cases: short group labels (`"basic"`, `"sort"`, `"encoding"`)

## Mocking with FFF

Use FFF when the module under test calls functions from other modules that you don't want to link. Declare fakes in the test file:

```c
#include "fff.h"
DEFINE_FFF_GLOBALS;

/* Fake psync_malloc to track allocations */
FAKE_VALUE_FUNC(void *, psync_malloc, size_t);

/* Fake psync_free (void return) */
FAKE_VOID_FUNC(psync_free, void *);
```

### FFF Patterns

**Reset fakes** between tests using a checked fixture:

```c
static void setup(void) {
  RESET_FAKE(psync_malloc);
  RESET_FAKE(psync_free);
  FFF_RESET_HISTORY();
}

/* In suite setup: */
tcase_add_checked_fixture(tc, setup, NULL);
```

**Set return values:**

```c
psync_malloc_fake.return_val = some_buffer;
```

**Return sequences** (different values on successive calls):

```c
void *returns[] = {buf1, buf2, NULL};
SET_RETURN_SEQ(psync_malloc, returns, 3);
```

**Custom fake** for complex behavior:

```c
static void *my_malloc(size_t sz) {
  return malloc(sz);  /* pass through to real malloc */
}
psync_malloc_fake.custom_fake = my_malloc;
```

**Verify calls:**

```c
ck_assert_uint_eq(psync_malloc_fake.call_count, 1);
ck_assert_uint_eq(psync_malloc_fake.arg0_val, 64);  /* last call's first arg */
```

## Best Practices

- **One behavior per test.** Each `START_TEST` block should test a single behavior or edge case. Prefer more small tests over fewer large ones.
- **Clean up.** Free all allocations in each test. Check runs tests in forked processes by default, but clean tests catch leaks early and work correctly with `CK_FORK=no`.
- **Test edge cases.** Empty inputs, single elements, NULL pointers, boundary values, overflow conditions.
- **No library globals.** Tests should not depend on `psync_init()` or library-wide state. Mock or stub what you need.
- **Keep helpers local.** Define helpers (`make_item`, `list_to_array`, etc.) as `static` functions at the top of the test file. Don't create shared helper libraries.
- **Compile only what you need.** Include the minimum set of `.c` files. Use the shim `pcompat.h` and FFF fakes to avoid pulling in unrelated modules.
