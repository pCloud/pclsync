# pclsync C Style Guide

A ruleset for an AI coding agent (Claude Code or similar) writing and reviewing C code in the **pclsync** library. Rules are written to be mechanically checkable wherever possible. Where judgment is required, the rule names the criterion the agent must apply.

This guide is calibrated to the actual codebase: **GNU C99**, single-tree layout (`*.c`/`*.h` in the repo root, no `include/<project>/` separation), POSIX `pthread` used directly, custom debug/log infrastructure in `pcore.h`/`plibs.c`. Sections that originally targeted C11/`<stdatomic.h>`/cross-platform threading abstractions have been adapted to match the project.

A short "**Project deviation**" or "**Not used in pclsync**" note appears wherever the rule's strict form does not match current practice. Where the project has consciously chosen against a rule, the deviation is noted; where the rule is aspirational and adopting it is desirable, that is also called out.

---

## §1 — Integer types

Use sized integer types from `<stdint.h>` (`uint32_t`, `int64_t`, etc.) for most non-trivial variables. Use POSIX semantic types (`size_t`, `ssize_t`, `off_t`, `pid_t`, `mode_t`, `time_t`, etc.) when the value comes from or goes to a syscall or libc function. Use plain `int` only in narrow circumstances.

### Use sized types (`uint32_t`, `int64_t`, etc.) for:

- Struct fields where size and layout matter.
- Anything serialized, hashed, sent over a wire, or persisted to disk.
- Anything counting bytes or elements where overflow matters.
- Bitfields and bitmasks (`uint32_t flags`).
- Array indices into large buffers.
- pclsync identifiers: `psync_folderid_t`, `psync_fileid_t` (both `uint64_t`), `psync_syncid_t`, `psync_eventtype_t` (both `uint32_t`).

### Use POSIX semantic types where the API requires them:

- `size_t` for `sizeof`, `strlen`, allocator sizes, and any value paired with a buffer length.
- `ssize_t` for `read`, `write`, `recv`, and similar return values.
- `off_t`, `pid_t`, `uid_t`, `gid_t`, `mode_t`, `time_t` — match the API.
- Do not truncate these into `int` "for simplicity."

### Use plain `int` for:

- Loop counters over small, statically-known ranges (`for (int i = 0; i < 4; i++)`).
- Boolean-ish return codes (`0` for success, `-1` for failure — pclsync convention; `psync_error` thread-local carries the detail).
- Small enums passed by value.
- File descriptors (POSIX convention; `int fd` everywhere).
- `argc`, `main`'s return, `errno` comparisons.

### Forbidden without justification:

`short`, `long`, `unsigned long`, `unsigned`. They are size-ambiguous across platforms. `long` in particular is 32-bit on Windows and 64-bit on Linux. Pick `int32_t` or `int64_t`.

**Project note:** existing code occasionally casts to `(unsigned long)` for `printf` of a `size_t` value (e.g. `debug(D_WARNING, "could not allocate %lu bytes", (unsigned long)size);`). New code should prefer `%zu` for `size_t` and the `<inttypes.h>` macros (`PRIu32`, `PRId64`, `PRIuPTR`) for sized types. Do not propagate the `(unsigned long)` cast pattern to new call sites.

### Sign rule:

If a value cannot logically be negative *and* arithmetic is performed on it, use unsigned. If subtraction is performed and the result might need to be negative, use signed. Do not over-use unsigned: `size_t a - size_t b` underflows when `a < b` and the comparison is silently wrong.

---

## §2 — Intrusive collections

The project uses intrusive collection primitives (`psync_list`, `psync_tree`, `psync_interval_tree_t`) embedded in containing structs, with `psync_list_element` / `psync_tree_element` (the project's `container_of` analogues) to recover the outer struct.

### Rules:

1. **Embed the node, never point to it.** `psync_list link;`, not `psync_list *link;`.
2. **Name node members by their role, not their type.** `queue_link`, `by_deadline`, `lru_link` — not `list`, `node`, `tree`. A struct often participates in multiple collections; the role-based name disambiguates.
3. **Every intrusive node member has a comment naming the lock or invariant that protects it.** No exceptions. This is the single highest-value rule for catching concurrency bugs in review.
4. **Use the project's `_element` macros** to recover the outer struct: `psync_list_element(node, type, member)`, `psync_tree_element(node, type, member)`, `psync_interval_tree_element(node)`. Never cast. Never assume the node is at offset zero.
5. **If a struct lives in N collections, document its lifecycle in a header comment** on the struct: who allocates it, what removes it from each collection, who frees it.
6. **Provide a "is this node linked?" check** (`psync_list_isempty(&x->queue_link)` or equivalent) and initialize all nodes to the unlinked state at construction (`psync_list_init(&x->queue_link)`, `PSYNC_LIST_STATIC_INIT(...)`, `PSYNC_TREE_EMPTY`). Freeing a still-linked node is the classic intrusive bug.

### Blessed primitives:

The project's blessed collection-primitive headers are:

- **`plist.h`** — Linux-kernel-style intrusive doubly-linked list. Type `psync_list`. Macros: `psync_list_init`, `psync_list_add_head`, `psync_list_add_tail`, `psync_list_add_before`, `psync_list_add_after`, `psync_list_del`, `psync_list_isempty`, `psync_list_for_each`, `psync_list_for_each_safe`, `psync_list_for_each_element`, `psync_list_element`. Sort/extract helpers in `plist.c`.
- **`ptree.h`** — Intrusive balanced (AVL-ish) binary tree. Type `psync_tree`. Helpers: `psync_tree_get_first`, `psync_tree_get_next`, `psync_tree_add`, `psync_tree_del`, `psync_tree_for_each_element`, `psync_tree_element`. Sentinel `PSYNC_TREE_EMPTY`.
- **`pintervaltree.h`** — Interval tree built on top of `psync_tree`. Type `psync_interval_tree_t`. API: `psync_interval_tree_add`, `psync_interval_tree_remove`, `psync_interval_tree_first_interval_containing_or_after`, `psync_interval_tree_free`, `psync_interval_tree_cut_end`.

Do not introduce new collection primitives without human review. If a need arises that none of these covers, raise it before writing code.

### Example:

```c
/*
 * struct work_item — a unit of work in the scheduler.
 *
 * Lifetime: allocated by work_item_create(), freed by work_item_destroy().
 *           Must be removed from queue_link and by_deadline before destroy.
 * Protection: queue_link and by_deadline are protected by scheduler.lock.
 */
struct work_item {
    uint32_t            id;
    uint64_t            submitted_ns;
    psync_list          queue_link;    /* protected by scheduler.lock */
    psync_tree          by_deadline;   /* protected by scheduler.lock */
    /* ... payload ... */
};
```

---

## §3 — Error handling and goto cleanup

The project uses Linux-kernel-style `goto cleanup` for error handling. Two variants are permitted depending on complexity. (Existing pclsync code uses both styles; new code should pick whichever is appropriate per the criteria below.)

### Single-label cleanup (`out:`)

Use when:

- Two or fewer resources to release.
- All resources are released unconditionally on every exit path.
- Each release function is null/zero-safe (`psync_free(NULL)`, `close(-1)` guarded, etc.).

```c
int load_config(const char *path, struct config *out)
{
    int    rc  = 0;
    int    fd  = -1;
    char  *buf = NULL;

    fd = open(path, O_RDONLY);
    if (fd < 0) { rc = -errno; goto out; }

    buf = psync_malloc(MAX_CFG);
    if (!buf) { rc = -ENOMEM; goto out; }

    /* ... parse ... */

out:
    psync_free(buf);      /* psync_free(NULL) is a no-op */
    if (fd >= 0) close(fd);
    return rc;
}
```

**Cost:** every cleanup runs on success too. **Benefit:** one exit point; very hard to leak.

### Staged-label cleanup (`err_close_fd:`, `err_free_buf:`)

Use when:

- Three or more resources, or
- Resources have order dependencies (must unmap before closing fd), or
- Some cleanup is expensive enough that running it on success is wasteful, or
- Some resources transfer ownership out on success and must not be freed in the success path.

```c
int build_pipeline(struct pipeline **out)
{
    int              rc;
    struct pipeline *p;
    int              fd_in  = -1;
    int              fd_out = -1;
    void            *region = MAP_FAILED;

    p = psync_new(struct pipeline);
    if (!p) return -ENOMEM;
    memset(p, 0, sizeof(*p));

    fd_in = open("/dev/foo", O_RDONLY);
    if (fd_in < 0)  { rc = -errno; goto err_free_p; }

    fd_out = open("/dev/bar", O_WRONLY);
    if (fd_out < 0) { rc = -errno; goto err_close_in; }

    region = mmap(NULL, SZ, PROT_READ, MAP_SHARED, fd_in, 0);
    if (region == MAP_FAILED) { rc = -errno; goto err_close_out; }

    p->in = fd_in;
    p->out = fd_out;
    p->region = region;
    *out = p;     /* ownership transfers to caller */
    return 0;

err_close_out: close(fd_out);
err_close_in:  close(fd_in);
err_free_p:    psync_free(p);
    return rc;
}
```

**Cost:** more labels to maintain. **Benefit:** each label cleans up exactly the resources acquired before that point, in reverse order.

### Hard rules for both styles:

1. **Initialize all owned resources to a sentinel** (`-1`, `NULL`, `MAP_FAILED`) at the top of the function, before the first thing that can fail.
2. **Single `return` at the bottom** (or one early return *before* any acquisition).
3. **Never `goto` forward across an acquisition.** Forward gotos skip cleanup setup.
4. **Never have two labels clean up the same resource.**
5. **Labels are ordered bottom-to-top in the same order resources were acquired.** A `goto err_X` is only legal if everything below `err_X:` has already been acquired at that point.

---

## §4 — Memory ownership and const

The project uses a naming and commenting convention to make ownership explicit at function signatures.

### Parameter and return naming

| Form                    | Meaning                                                                                                  |
| ----------------------- | -------------------------------------------------------------------------------------------------------- |
| `const T *x`            | Borrowed, read-only. Caller retains ownership. Function must not store the pointer past the call.        |
| `T *x`                  | Borrowed, mutable. Caller retains ownership. Function may mutate `*x` but must not free or store it.     |
| `T *x_in` (param)       | Ownership transfers *into* the function on success. See conditional-transfer rule below.                 |
| `T **x_out` (param)     | Out-parameter. Function writes a value (often newly-allocated, owned by caller on success).              |
| `T **x_inout` (param)   | Function reads and may replace. Ownership of the replaced value transfers to the function (it frees the old). |
| Return `T *` (non-null) | Ownership transfers to caller. Caller must `psync_free` (or call the type's specific destructor).        |
| Return `const T *`      | Borrowed view into something the callee owns. Caller must not free, must not outlive the source.         |

**Project note:** the `_in`/`_out`/`_inout` parameter suffixes are a *new-code convention* introduced by this guide. Existing pclsync APIs do not use them; do not retrofit. Apply the convention only to new functions where ownership is non-obvious from context.

### Header comment convention

Every non-static function gets a one-line ownership note when ownership is non-obvious:

```c
/* Parses cfg text and returns a newly-allocated config.
 * Caller owns; free with config_free().
 * `text` is borrowed; safe to free immediately after this returns. */
struct config *config_parse(const char *text);

/* Takes ownership of `item` on success (returns 0).
 * On failure, caller still owns `item`. */
int queue_push(struct queue *q, struct work_item *item_in);

/* Returns a borrowed view valid until the next config_reload() or config_free(). */
const char *config_get_string(const struct config *cfg, const char *key);
```

### Hard rules:

1. **If a function stores a pointer parameter into a struct field that outlives the call**, that parameter must be named `_in` (transfer) or the function must `psync_strdup`/copy it. No silently-aliasing borrowed pointers.
2. **`const T *` parameters may not be cast to `T *` inside the function.** Ever.
3. **Out-parameters (`T **out`) are set to `NULL` on every error path.** This is the project-wide convention.
4. **Functions that conditionally take ownership must say so explicitly** in their comment, like `queue_push` above.

### Failure semantics for transfer-in:

**On failure, the caller still owns the input.** This is the project-wide convention. A function with an `_in` parameter that returns an error code has *not* consumed the input; the caller's cleanup code is uniform.

---

## §5 — Header organization and build-conditional routing

### Project layout

pclsync uses a **flat layout**: all `.c` and `.h` files live in the repository root. There is no `include/<project>/` separation; public and private headers are intermixed and included as `#include "foo.h"` (quotes) throughout. A handful of headers are also surfaced for callers via the build (e.g. `psynclib.h`, `pcompat.h`, `pcompiler.h`, `psyncer.h`, `pstatus.h`).

When introducing a new module:

- Place `<module>.h` and `<module>.c` in the repo root alongside existing files.
- Prefer the existing `psync_` symbol prefix and the `_PSYNC_<MODULE>_H` include-guard convention.
- Functions that only one `.c` file needs are `static`. Anything called from another `.c` lives in the matching `.h`.

### Header rules

1. **Self-contained:** every type and macro used is either defined in the header or pulled in via its own `#include`.
2. **Use include guards** in the project's existing form: `#ifndef _PSYNC_FOO_H` / `#define _PSYNC_FOO_H` / `#endif`.
3. **Forward-declare struct types** when only pointers are used. Full definitions only when callers need the size or fields.
4. **A `.c` file's own header is its first include** — if it does not compile standalone, this catches it. New files should follow this even though some existing files do not.
5. Compile cleanly on every supported platform without consumer code needing to define platform macros first. `pcompat.h` already establishes `P_OS_*`.

### Include order in `.c` files

Top to bottom, with a blank line between groups (new code; existing files vary):

1. The matching header for this `.c` file.
2. C standard library (`<stdio.h>`, `<stdlib.h>`, `<string.h>`, ...).
3. POSIX system headers (`<unistd.h>`, `<pthread.h>`, ...). Win32 headers when building Windows-specific code.
4. Third-party libraries (`<sqlite3.h>`, `<openssl/...>`, `<fuse.h>`, ...).
5. Project headers (`"pcompat.h"`, `"pcompiler.h"`, `"plibs.h"`, ...).

### Forbidden

- `#include` inside a function body or struct definition.
- Conditional `#include` based on anything other than OS macros (`P_OS_*`), compiler macros (`__GNUC__`, `_MSC_VER`), feature-test macros (`_FILE_OFFSET_BITS`), build-system-defined macros (`P_SSL_*`, `IS_DEBUG`), or library-config macros (`P_ELECTRON`).
- Re-including a header to get different behavior based on macros (X-macros are the rare exception and require a comment).

### Visibility

Any non-`static` function with no declaration in any header is a bug — it is either dead code or it is leaking out of its module.

### Build-conditional routing — current pclsync practice

pclsync has two routed modules. Neither uses the strict "router + contract + variant header" split prescribed by some codebases; document and preserve the existing pattern.

#### `pssl.h` — SSL/TLS backend selection

`pssl.h` is both router *and* declaration header. It selects one of:

| `USESSL=` value     | Discriminating macro     | Variant header           | Variant `.c`               |
| ------------------- | ------------------------ | ------------------------ | -------------------------- |
| `openssl`           | `P_SSL_OPENSSL`          | `pssl-openssl.h`         | `pssl-openssl.c`           |
| `openssl3`          | `P_SSL_OPENSSL3`         | `pssl-openssl3.h`        | `pssl-openssl3.c`          |
| `mbed`              | `P_SSL_MBEDTLS`          | `pssl-mbedtls.h`         | `pssl-mbedtls.c`           |
| `wolfssl`           | `P_SSL_WOLFSSL`          | `pssl-wolfssl.h`         | `pssl-wolfssl.c`           |
| `securetransport`   | `P_SSL_SECURETRANSPORT`  | `pssl-securetransport.h` | `pssl-securetransport.c`   |

The discriminating macro is set by the top-level `Makefile` (`-DP_SSL_OPENSSL` etc., gated on `USESSL=`). The router's `#if/#elif/#else` chain ends in `#error "Please specify SSL library to use"`. Each variant header defines the concrete types (the `psync_ssl_*` opaque structs); `pssl.h` declares the cross-variant API (`psync_ssl_init`, `psync_ssl_connect`, …) and constants directly.

When adding to the SSL surface:

1. Declare the new function in `pssl.h` (the cross-variant interface).
2. Implement it in *every* variant `.c` file. A missing implementation produces a link error — that's the contract.
3. If the function genuinely cannot be supported on a backend, provide a stub returning `PSYNC_SSL_FAIL` (or equivalent) and `debug(D_WARNING, ...)` rather than `#ifdef`-ing out the call site.

#### `poverlay.c` — per-OS overlay backend

`poverlay.c` switches at the source level: `#if defined(P_OS_WINDOWS) #include "poverlay_win.c" #elif defined(P_OS_LINUX) #include "poverlay_lin.c" #elif defined(P_OS_MACOSX) #include "poverlay_mac.c" #else /* stubs */ #endif`. The `poverlay_*.c` files are not compiled standalone; they are included into `poverlay.c`. The macros are set by `pcompat.h` from compiler-defined ones (`__linux__`, `__APPLE__`, `_WIN32`).

#### Routing rules for new code

1. The router's `#if` chain ends in `#else #error "<module>: no variant selected for this configuration"`. No silent fallthrough.
2. The router's header comment names, for every macro it tests, where that macro is defined (toolchain, build system, `pcompat.h`).
3. Order branches most-specific to least-specific.
4. Adding a function to a routed surface (e.g. `pssl.h`) means adding it to *every* variant. A missing implementation produces a link error.
5. A function that genuinely exists only in some variants is still declared in the routing header, with variants that do not support it providing a stub returning `-1` / `PSYNC_SSL_FAIL` / equivalent. Call sites use `#ifdef` only when the *caller's own logic* is variant-specific.
6. Routing headers are not nested. A variant header does not itself route to further sub-variants.
7. Standard C headers (`<string.h>`, `<stdlib.h>`, `<stdint.h>`, `<errno.h>`, etc.) are included directly by each `.c` file that uses them.
8. Feature-test macros (`_FILE_OFFSET_BITS`, `_LARGEFILE64_SOURCE`) are defined via the Makefile, not in source files.
9. Any new conditional include carries a comment naming where the controlling macro is defined.

### Compiler attributes

The project's compiler-attribute header is **`pcompiler.h`**. Macros are prefixed `PSYNC_*`:

| Macro                              | Purpose                                                        |
| ---------------------------------- | -------------------------------------------------------------- |
| `PSYNC_NOINLINE`                   | Discourage inlining of this function                           |
| `PSYNC_MALLOC`                    | Function returns a fresh, non-aliased allocation               |
| `PSYNC_SENTINEL`                   | Variadic function ends with a `NULL` sentinel                 |
| `PSYNC_PURE`                       | No side effects, depends only on args + global memory          |
| `PSYNC_CONST`                      | No side effects, depends only on args                          |
| `PSYNC_COLD`                       | Rarely executed (logging, error paths)                         |
| `PSYNC_FORMAT(archetype, str, va)` | `printf`-like format string check                              |
| `PSYNC_NONNULL(...)`              | Listed pointer args must be non-null                           |
| `PSYNC_PACKED_STRUCT`              | Packed struct (no padding)                                     |
| `PSYNC_THREAD`                     | Thread-local storage (`__thread` / `__declspec(thread)`)       |
| `likely(expr)` / `unlikely(expr)`  | Branch hints                                                   |
| `psync_prefetch(p)`                | Prefetch hint                                                  |
| `psync_alignof(t)`                 | `alignof` (works pre-C11)                                      |
| `psync_atomic_*_uint32 / uint64`   | Atomic load/set/add/cas (built on `__sync_*` / `Interlocked*`) |

Any compiler-specific annotation goes through these macros. Raw `__attribute__` or `__declspec` outside `pcompiler.h` is not allowed in new code.

---

## §6 — Allocation discipline

All heap allocation goes through the project's wrappers. Direct calls to `malloc`, `calloc`, `realloc`, `free`, `strdup`, `strndup`, `aligned_alloc`, `posix_memalign` are not allowed in project code outside of `psynclib.c` and `pmemlock.c` (which implement the wrappers themselves).

### The wrappers

Defined in `psynclib.h` (`psync_malloc`, `psync_realloc`, `psync_free`), `pmem.h` (`psync_new`, `psync_new_cnt`, `psync_memclean`), `pstrings.h` (`psync_strdup`, `psync_strndup`, `psync_strcat`), `pmemlock.h` (`psync_locked_malloc`, `psync_locked_free`):

| Wrapper                          | Use for                                                                               |
| -------------------------------- | -------------------------------------------------------------------------------------|
| `psync_malloc(size)`             | General heap allocation. **Aborts on OOM after retry**, so callers do not need to NULL-check the return — but on `IS_DEBUG` builds, allocations are filled with `0xfa` poison, so do not assume zero. |
| `psync_realloc(ptr, size)`       | Resize. Aborts on OOM after retry.                                                   |
| `psync_free(ptr)`                | Release. NULL-safe.                                                                  |
| `psync_new(type)`                | Convenience macro: `(type *)psync_malloc(sizeof(type))`.                             |
| `psync_new_cnt(type, cnt)`       | Convenience macro: `(type *)psync_malloc(sizeof(type)*(cnt))`.                       |
| `psync_strdup(str)`              | Wrapped `strdup` (returns `psync_malloc`'d copy). Uses `psync_malloc` underneath.    |
| `psync_strndup(str, n)`          | Wrapped `strndup`.                                                                   |
| `psync_strcat(s1, s2, ..., NULL)`| Sentinel-terminated varargs concat; returns `psync_malloc`'d.                        |
| `psync_locked_malloc(size)` / `psync_locked_free(ptr)` | Pinned (mlock'd) memory for crypto material. See `pmemlock.c`.    |
| `psync_memclean(ptr, len)`       | Volatile zeroize (compiler cannot elide). Use on key material.                       |

### Hard rules

1. **All allocations go through project wrappers.** Direct libc allocator calls are not allowed in code outside `psynclib.c` / `pmemlock.c` / `pcompat.c` (where the wrappers are implemented). Exception: third-party headers configured with their own allocators (e.g. mbedTLS hooks) are permitted to call libc directly inside their wrapper layer.
2. **`psync_malloc` does not zero-initialize.** Debug builds fill with `0xfa` poison. If you need zeroed memory, allocate then `memset(p, 0, size)` — there is no project `zalloc`/`calloc` wrapper.
3. **`psync_malloc` aborts on OOM** (after one round of cache-flushing retry). New code may omit the NULL check on `psync_malloc` / `psync_realloc` / `psync_strdup` returns; existing code may have NULL checks for legacy reasons. Do not introduce *new* NULL-check error-recovery paths for the standard wrappers — the wrapper's contract is "succeeds or aborts."
4. **Never use the realloc anti-pattern `p = psync_realloc(p, n);` if you intend to handle failure.** Since `psync_realloc` aborts on OOM, the assignment is safe in pclsync. If you ever introduce a failable allocator variant, the safe pattern is:
   ```c
   void *tmp = my_realloc(p, n);
   if (!tmp) { rc = -ENOMEM; goto err; }
   p = tmp;
   ```
5. **Multiplications for allocation size (`n * sizeof(T)`) overflow silently.** When `n` comes from outside the process, check `n <= SIZE_MAX / sizeof(T)` before allocating. See §7.
6. **Use `sizeof(*ptr)` not `sizeof(struct foo)` when allocating.** `p = psync_new(struct foo)` (which expands to `sizeof(struct foo)`) is the canonical pattern; for non-struct or generic allocations, prefer `p = psync_malloc(sizeof(*p));` so it stays correct if the type later changes.
7. **Every allocation has a corresponding free path reachable on every error branch.** §3's goto-cleanup discipline enforces this.
8. **After `psync_free(p)`, set `p = NULL`** if `p` is a struct field or could be read again. For locals about to go out of scope, do not bother.
9. **VLAs are permitted only via the project macro `psync_def_var_arr(name, type, size)`** in `pcompat.h`. The macro expands to a real VLA on GCC and to `alloca()` on other compilers. New code prefers `psync_malloc`/`psync_free` over `psync_def_var_arr` unless the size is a tiny, statically-bounded value and the call site is hot. Raw VLAs (`type buf[n]` with runtime `n`) outside this macro are not allowed.
10. **Bare `alloca()` is not allowed** outside the `psync_def_var_arr` macro.

### Aligned allocation

**Not used in pclsync.** The codebase has no project-wide aligned-allocation wrapper. If a hot path needs SIMD- or cache-line-aligned memory, raise it with the maintainers — adding such a wrapper to `pmem.h` is the right place. Do not pun pointers from misaligned `psync_malloc` results — it is UB on strict-alignment targets.

### `strdup` and friends

**Use `psync_strdup` / `psync_strndup`.** Direct `strdup`/`strndup` calls in project code are allocations that bypass the project wrapper layer. The codebase already follows this rule consistently — preserve it.

`psync_strcat(...)` (sentinel-terminated varargs concat) returns a `psync_malloc`'d buffer. `psync_slprintf` and `psync_strlcpy` (in `pcore.h`) are non-allocating bounded-copy helpers.

---

## §7 — Integer arithmetic safety

### Overflow

1. **Signed overflow is UB. Unsigned overflow wraps.** Treat both as bugs unless wrap is intentional and commented (`/* intentional wrap: monotonic counter */`).
2. **For untrusted inputs (sizes from a wire protocol, user-supplied counts), use checked arithmetic.** GCC has `__builtin_add_overflow`, `__builtin_mul_overflow`, etc. These are available on the project's GCC/Clang baseline. Use them directly in new code; if a wrapper appears (`add_overflow_u64`, ...) prefer it.
3. **Any arithmetic on values from outside the process** (file, socket, ioctl, env var, binary API response) must use checked arithmetic or have an explicit range check before the operation.
4. **Before allocating `n * sizeof(T)`, check `n <= SIZE_MAX / sizeof(T)`** explicitly. There is no project array-allocator.

### Mixing signed and unsigned

1. **Don't compare signed to unsigned** without thinking. Pick one type for both sides. (`-Wsign-compare` is not currently enabled in the project's CFLAGS — see §12 — so the compiler won't warn you.)
2. **Subtraction of unsigneds is the classic trap.** `size_t a = 5, b = 10; if (a - b < 0)` is never true; `a - b` is `SIZE_MAX - 4`. Never subtract unsigneds without first checking `a >= b`, or do the math in a signed type.
3. **Casts between signed and unsigned require a comment** explaining why the value is in range. No silent casts on non-trivial values.

### Truncation

1. **Narrowing conversions** (`int64_t → int32_t`, `size_t → int`) are bugs unless explicitly range-checked first.
2. **`int` ↔ `size_t` is a constant pain point.** Never do `int len = strlen(s);` — that's a 2GB truncation waiting to happen.

### Division and modulo

1. **Always check the divisor is non-zero** before `/` or `%`, unless the divisor is a compile-time constant.
2. **`INT_MIN / -1` is UB on signed types.** If the dividend can be the minimum value of the type, check.

### Shifts

1. **Shifting a signed integer left into the sign bit is UB.** Use unsigned types for bit manipulation: `uint32_t flags = 1u << 31;`.
2. **Shifting by >= the type width is UB.** `uint32_t x; x >> 32` is UB, not zero. If shift counts are runtime values, mask: `x >> (n & 31)` or guard.

### Boolean from arithmetic

Comparisons must be explicit. `if (flags & FLAG_X)` is fine. `if (some_count)` is allowed for "non-zero means present" but `if (some_count > 0)` and `if (ptr != NULL)` are clearer and preferred.

---

## §8 — Strings and buffers

### NUL-termination policy

**pclsync uses NUL-terminated C strings throughout.** Functions that take a `const char *` may assume it is NUL-terminated unless explicitly documented otherwise. Functions that produce a string produce a NUL-terminated one.

There is no project-wide policy of length-paired buffers (no `(data, len)` parameter convention). When a length is needed, it is computed via `strlen` at the boundary. The exceptions are binary-data APIs in `papi.h` / `pssl.h` / `pfscrypto.c`, which take explicit `size_t len` because the data is not text.

When buffers do hold binary data (network packets, file contents, hashes), use `uint8_t *` or `void *` plus a size; do not pretend they are strings. `char *` means "this is text."

### Banned functions, no exceptions

- `strcpy`, `strcat`, `sprintf`, `gets`, `vsprintf` — no length limit, unbounded write.
- `scanf("%s", ...)` and `fscanf("%s", ...)` without an explicit width.
- `strtok` — modifies input, not thread-safe, holds hidden state. Use `strtok_r` (POSIX) or write an explicit tokenizer.
- `atoi`, `atol`, `atof` — no error reporting, undefined behavior on overflow. Use `strtol`/`strtoul`/`strtod` (or the project's `psync_ato32`/`psync_ato64` in `pstrings.h` when the input is trusted).

### Restricted, with rules

- **`strncpy`** — does *not* guarantee NUL-termination, and pads with zeros up to the limit. Almost always wrong. Use `psync_strlcpy(dst, src, dst_size)` for "copy with truncation that stays NUL-terminated." If you genuinely need `strncpy` semantics (fixed-width fields in a binary record), comment why.
- **`strncat`** — its length argument means "max chars to *append*," not "size of buffer." Avoid.
- **`snprintf`** — the right tool for bounded formatting, but the return value is the length that *would have been written* if the buffer were large enough, not the length actually written. Truncation detection requires `if (ret < 0 || (size_t)ret >= dst_size)`. Always check. The project also provides `psync_slprintf` (in `pstrings.h`) which is a thin wrapper.
- **`memcpy` / `memmove`** — `memcpy` is UB if regions overlap. If overlap is possible or unclear, use `memmove`.

### Project string helpers

| Function                 | Use                                                                        |
| ------------------------ | -------------------------------------------------------------------------- |
| `psync_strdup(s)`        | Allocate a NUL-terminated copy. Wrapper around `psync_malloc`.            |
| `psync_strndup(s, n)`    | Allocate a copy of at most `n` bytes, NUL-terminated.                     |
| `psync_strcat(s1, s2, ..., NULL)` | Sentinel-terminated varargs concat. Returns `psync_malloc`'d.    |
| `psync_strlcpy(dst, src, size)`   | BSD-style length-bounded copy (always NUL-terminates if `size > 0`). Returns the length of `src`. |
| `psync_slprintf(dst, size, fmt, ...)` | `snprintf` wrapper.                                              |
| `psync_strnormalize_filename(s)`  | Allocate normalized form for filename comparisons.                |
| `psync_match_pattern(name, pat, plen)` | Glob match.                                                  |
| `psync_ato32(s)` / `psync_ato64(s)` | Trusted-input numeric parsing; no error reporting.              |
| `psync_is_valid_utf8(s)` | UTF-8 validation.                                                          |

### Buffer-with-length type

**Not used in pclsync.** No `(data, len, cap)` struct exists. Where the codebase does need a growable buffer, it allocates with `psync_malloc`, tracks length in a sibling local, and grows via `psync_realloc`. A buffer-with-length struct is fine to introduce when a third call site would otherwise grow such a triple in its signature — raise it with maintainers first.

### String-builder helper

**Not used in pclsync.** When accumulating with `snprintf`, the explicit pattern is:

```c
size_t off = 0;
int    n;

n = snprintf(buf + off, buf_size - off, "...");
if (n < 0 || (size_t)n >= buf_size - off) { rc = -ENOMEM; goto err; }
off += (size_t)n;

n = snprintf(buf + off, buf_size - off, "...");
if (n < 0 || (size_t)n >= buf_size - off) { rc = -ENOMEM; goto err; }
off += (size_t)n;
```

Note the `(size_t)n` cast and the explicit `buf_size - off` (the remaining space, not the original size). For unbounded growth, `psync_strcat(...)` covers the simple cases.

### Format strings

1. **Format strings passed to `printf`/`snprintf`/`fprintf`/`debug` family must be string literals, never user input or computed at runtime.** `printf(user_input)` is a CVE; `printf("%s", user_input)` is correct. The `debug(level, fmt, ...)` macro requires the same.
2. **Use `<inttypes.h>` macros for sized types** in new code: `debug(D_NOTICE, "id %" PRIu32, x)`, `PRId64` for `int64_t`, `PRIuPTR` for `uintptr_t`. Use `%zu` for `size_t`. The existing code occasionally casts to `(unsigned long)` and uses `%lu`; do not propagate that pattern.
3. **Sign-extension surprises:** `printf("%x", ch)` where `ch` is `char` will sign-extend on platforms where `char` is signed. Cast to `unsigned char` first.

### Char vs. byte

- **`char`** is for C strings.
- **`uint8_t *`** (or `unsigned char *`) is for binary buffers.
- **`void *`** is for opaque buffers in interfaces where the caller's type is irrelevant.

In a new function signature, if the buffer holds bytes (network packets, file contents, hashes), it is `uint8_t *` or `void *`. `char *` means "this is text."

---

## §9 — Assertions, runtime checks, and error returns

The three are distinct and must not be confused:

- **Assertions** are for programmer errors: things the code's author knows must be true. If they are false, there is a bug in the code.
- **Runtime checks** are for environmental conditions: things that can be true or false depending on inputs, OS state, or other components we don't control. They produce error returns.
- **Error returns** are how runtime check failures are communicated up the call stack.

### Assertions in pclsync

The project's assertion macros live in `pcore.h`:

- **`assert(cond)`** — aborts when `cond` is false. **Compiles out** when `DEBUG_LEVEL < D_WARNING`. (This shadows libc's `assert`; `pcore.h` `#undef`s it before redefining.)
- **`assertw(cond)`** — logs a `D_WARNING` message but does *not* abort. Compiles out at the same level.
- **`likely_log(x)` / `unlikely_log(x)`** — branch hint plus a `D_WARNING` log on the unexpected path. Useful for "this is supposed to be true; if it isn't, I want to know." Compiles down to the bare `likely`/`unlikely` when `DEBUG_LEVEL < D_WARNING`.
- **`PRINT_RETURN(x)` / `PRINT_NEG_RETURN(x)`** — returns `x` and logs the value. Compiles out below `D_WARNING`. Use sparingly; mostly for tracing failure-path returns.

Use `assert` for:

1. Function preconditions that the *caller* is responsible for: "this pointer is non-NULL," "this index is in range," "this state is one of {A, B, C}."
2. Internal invariants: "we just inserted, so size > 0," "this lookup hit, so the entry exists."

#### Rules

1. **Assertions must have no side effects.** `assert(do_thing() == 0)` is wrong — `do_thing` will not run when assertions are disabled. Compute first, then assert.
2. **Assertions must not be used for runtime checks.** `assert(psync_malloc_result != NULL)` is wrong — the wrapper aborts on OOM anyway.
3. **Assertions must not be used for input validation at API boundaries.** Bad inputs from outside the program are runtime errors.
4. **For invariants whose violation would corrupt state**, use `abort()` directly (with a `debug(D_CRITICAL, ...)` first) — it is *not* compiled out. The codebase has ~20 such sites; follow the same pattern.

#### "Abort even in release" macro

**Not present in pclsync.** There is no project-wide `BUG_ON` / `panic`. The convention is `debug(D_CRITICAL, "..."); abort();` inline. If a third call site appears that would benefit from a named macro, propose adding `psync_panic(fmt, ...)` to `pcore.h`. Until then, write the `debug` + `abort` pair explicitly with a comment naming the invariant violated.

### Runtime checks and error returns

1. **Most pclsync functions that can fail return `int`** with `0` for success and `-1` for failure. The thread-local `psync_error` (declared in `pcore.h`) carries the specific error code (`PERROR_*` constants from `psynclib.h`). This is the existing convention; preserve it. New low-level helpers may return negative `errno` values directly when no `PERROR_*` mapping fits.
2. **Functions that produce a value** use an out-parameter for the value and the int return for the status, *or* return a pointer (NULL on failure with `psync_error` set). Both patterns are present; either is acceptable for new code.
3. **Error returns are checked at every call site.** If the caller genuinely doesn't care (e.g. best-effort cleanup), the return is explicitly cast to `(void)`:
   ```c
   (void)close(fd);  /* best-effort during error cleanup */
   ```
   This marks "I considered this and chose to ignore," not "I forgot."
4. **Functions that can't fail return `void`.** Don't have functions that always return `0` — it trains callers to ignore the return.
5. **Don't smuggle errors through globals or thread-locals** beyond the existing `psync_error` and `errno`. Do not introduce new global-error mechanisms.

### `errno` discipline

1. **After a syscall or libc function fails, capture `errno` into a local *immediately*** before doing anything else — including logging or further calls. Anything can clobber it:
   ```c
   fd = open(path, O_RDONLY);
   if (fd < 0) {
       int err = errno;
       debug(D_WARNING, "open %s failed: %s", path, strerror(err));
       return -1;
   }
   ```
2. **Don't read `errno`** unless the preceding call documented that it sets it.
3. **`strerror` is not thread-safe.** The codebase uses it directly in many places; a pclsync wrapper does not currently exist. New code should prefer `strerror_r` (POSIX) when reading `errno` from a multi-threaded context, even though existing code does not. Format the result before returning to anything that might clobber the buffer.

### Logging vs. returning

The agent does not both log *and* return an error at the same level *unless that layer is the designated logging point*. The project does not formally name a "log and propagate" layer; in practice, `pdiff.c`, `pupload.c`, `pdownload.c`, `pfs.c`, and similar top-level orchestrators log via `debug(D_*, ...)` and return errors, while lower-level helpers return errors silently and let the caller decide. New code should follow this layering: leaf functions in modules like `papi.c`, `pssl.c`, `pcrypto.c` should *not* log on every error — let the caller log with the context it has.

---

## §10 — Threading, atomics, and locking

The project uses a **shared-state, multi-threaded model**: multiple threads, shared data structures, locks. POSIX `pthread` is used directly (no abstraction module). Atomics use the project's `psync_atomic_*` macros built on `__sync_*` GCC builtins (or `Interlocked*` on MSVC).

### Locking discipline

1. **Every mutable data structure shared across threads has exactly one lock that protects it**, named in a comment on the struct definition.
2. **Every field gets a comment naming its protection:** a lock, "atomic," "immutable after init," "thread-local," or "owned by thread X."
3. **Functions that require a lock to be held name it in their comment, and ideally in their name** (the `_locked` suffix is the convention; existing pclsync code uses both `_locked` and inline-only helpers — `_locked` is preferred for new code):
   ```c
   /* Caller must hold s->lock. */
   static void scheduler_enqueue_locked(struct scheduler *s, struct work_item *w);
   ```
   Functions without the suffix acquire and release the lock themselves.
4. **Lock ordering:** if a function may hold two locks at once, they are always acquired in the documented order. Violations of the documented order are bugs even when they happen not to deadlock in practice.
5. **Don't call out to unknown code** (callbacks, API client functions that may take locks of their own, library calls that might re-enter your code) while holding a lock. If you must, document it.
6. **Don't hold a lock across blocking I/O** unless that's the entire point of the lock.

### Struct comment convention

Every new struct that may be accessed from more than one thread declares its protection in the struct's header comment, not just per-field. The struct comment names: which lock protects most of its fields (the "primary lock"), any fields with different protection, and the lifetime/ownership rules.

```c
/*
 * struct scheduler — work queue and deadline tree.
 *
 * Protection: scheduler.lock protects all fields except where noted.
 * Lifetime:   created by scheduler_create(), freed by scheduler_destroy().
 *             No references may outlive scheduler_destroy().
 * Lock order: scheduler.lock is acquired before any work_item.lock.
 */
struct scheduler {
    pthread_mutex_t   lock;
    psync_list        queue;          /* protected by lock */
    psync_tree       *by_deadline;    /* protected by lock */
    uint32_t          generation;     /* atomic via psync_atomic_*_uint32 */
    const char       *name;           /* immutable after init */
};
```

### Lock primitives

| Type                          | Use                                                                                                  |
| ----------------------------- | ---------------------------------------------------------------------------------------------------- |
| `pthread_mutex_t` + `pthread_mutex_lock/unlock` | Plain mutual exclusion. The `pcore.h` macros wrap lock/unlock to abort on error in debug builds. |
| `pthread_cond_t`              | Condition variable. Always check the predicate in a loop around the wait — see below.               |
| `psync_rwlock_t` (`plocks.h`) | Read-write lock with optional read-starves-writer (`psync_rwlock_rdlock_starvewr`) and timed-acquire (`psync_rwlock_timedrdlock`/`timedwrlock`). Has built-in caller-holds-lock predicates (`psync_rwlock_holding_*`) — useful for `_locked`-suffix function preconditions. |
| `psync_atomic_*_uint32` / `_uint64` | Atomic operations on 32- or 64-bit counters, in `pcompiler.h`.                              |
| `PSYNC_THREAD`                | Thread-local storage. Use this, *not* C11's `_Thread_local`. `pcompiler.h` defines it as `__thread` on GCC/Clang and `__declspec(thread)` on MSVC. |

### Lock-order documentation

**Not formally documented in pclsync.** The convention until further notice is: each lock's struct definition comment names what it protects and any locks that must be acquired before/after it. When introducing a new lock, document the order on the struct. Once a third lock with cross-locking relationships exists, propose adding `docs/locking.md` to centralize the global order.

### Atomics

1. **Use `pcompiler.h`'s `psync_atomic_*_uint32` / `_uint64`** for cross-thread integer access not protected by a lock. **Do not use `<stdatomic.h>`** — pclsync is GNU C99 and the project does not enable C11 atomics. **Don't use `volatile` for thread synchronization** — `volatile` is for memory-mapped I/O, not for thread visibility.
2. **Memory ordering is implicit (`__sync_*` builtins are full barriers).** This is heavier than necessary but is the project's existing baseline. Do not introduce raw `__atomic_*` builtins with explicit ordering without coordinating with maintainers.
3. **Compound operations use the dedicated macros:** `psync_atomic_add_uint32`, `psync_atomic_compare_and_set_uint32`. Don't roll your own with separate load/store.

### Threading abstraction

**Not used in pclsync.** POSIX `pthread` is used directly throughout the codebase. Windows builds are limited and the codebase's Windows path uses the same `pthread_*` API via a Win32 pthreads shim or stubs. Do not invent a parallel abstraction module.

For starting a named worker thread, use the project's `psync_run_thread` / `psync_run_thread1` (in `pcompat.h`) — they set the `psync_thread_name` thread-local and handle detached threads.

### Thread-local storage

1. **Use `PSYNC_THREAD`** (from `pcompiler.h`). Do not use `_Thread_local` — pclsync is C99.
2. **Thread-locals get the protection comment** too: `/* thread-local */` next to the declaration.
3. **Thread-local destructors don't exist** in C99 with `__thread`. If a thread-local owns resources, the thread cleans up explicitly before exiting (or use `pthread_key_create` with a destructor).

### Condition variables and signaling

1. **Always check the predicate in a loop** around the cond-wait — spurious wakeups are real:
   ```c
   pthread_mutex_lock(&q->lock);
   while (q->count == 0 && !q->shutdown) {
       pthread_cond_wait(&q->not_empty, &q->lock);
   }
   /* ... */
   pthread_mutex_unlock(&q->lock);
   ```
2. **Signal/broadcast while holding the lock** that protects the predicate, or be very sure of why you're not. Signaling outside the lock is allowed but requires a comment explaining why.

### Once-initialization

Use `pthread_once` (POSIX) for one-time initialization. Do not roll your own with `if (!initialized) { initialize(); initialized = 1; }` — that's racy.

### What the agent must not do

1. Introduce a thread, a lock, or an atomic without a comment explaining what it protects, what invariant it preserves, or what handoff it implements.
2. Add a new field to a shared struct without specifying its protection.
3. Use `volatile` for thread synchronization. Ever.
4. Use `sleep` / `usleep` for synchronization. Ever.
5. Catch a deadlock by adding a `trylock` and retrying. That's a band-aid; either the lock order is wrong or the design is wrong.

---

## §11 — Logging and observability

### Logging primitives

`pcore.h` defines:

```c
#define debug(level, fmt, ...) \
    do { if (level <= DEBUG_LEVEL) psync_debug(__FILE__, __FUNCTION__, __LINE__, level, fmt, __VA_ARGS__); } while (0)
```

`psync_debug` is declared in `pcore.h` with `PSYNC_FORMAT(printf, 5, 6)` — the compiler validates `printf`-style format/arg consistency. Implementation lives in `plibs.c`; output goes to `DEBUG_FILE` (`/tmp/psync_err.log` on POSIX) or via `DEBUG_LEVELS` mapping.

### Log levels

| Level       | Numeric | Use for                                                                            |
| ----------- | ------- | ---------------------------------------------------------------------------------- |
| `D_BUG`      | 10      | Code-bug-level severity. Should never fire in correct code.                       |
| `D_CRITICAL` | 20      | Unrecoverable error; usually paired with `abort()`.                               |
| `D_ERROR`    | 30      | Operation failed; user/sync state may be affected.                                |
| `D_WARNING`  | 40      | Something unexpected; recovered or retried.                                       |
| `D_NOTICE`   | 50      | Informational; the default `DEBUG_LEVEL` floor.                                   |

Higher numeric value = lower priority. `DEBUG_LEVEL` defaults to `D_NOTICE`; messages with `level > DEBUG_LEVEL` are compiled out.

### Format

`debug(level, fmt, ...)` takes a `printf`-style format and arguments. **Format strings must be string literals.** No structured-field syntax (no key=value, no JSON) — log lines are free-form printf output. If a structured format becomes necessary, raise it with maintainers before introducing it ad-hoc.

### Hot-path restrictions

- Inside tight inner loops (per-page-cache-page, per-syscall-in-a-burst), do not log at any level. The `level <= DEBUG_LEVEL` check is the only thing keeping `D_NOTICE` calls out of release; that check is fast but not free.
- `D_NOTICE` is acceptable for once-per-operation messages. `D_WARNING` and above are acceptable on any path.
- For per-operation tracing, use `PRINT_RETURN(x)` / `PRINT_NEG_RETURN(x)` (from `pcore.h`) which are no-ops below `D_WARNING`.

### Never log

- **Secrets** — auth tokens (`psync_my_auth`), private keys, AES keys, RSA private keys, password hashes.
- **PII** — beyond what is intrinsic to the error context (logging a path that the user owns is fine; logging an arbitrary buffer's contents because it might "help" is not).
- **Raw user-supplied input** without escaping. Filename paths logged for debugging are fine; logging *content* of a file or *content* of a network response body is not, unless explicitly stripped of secrets and bounded.
- **Full request/response bodies** in production. Truncate or summarize.

### `errno` logging

Capture `errno` into a local *immediately* after a failed syscall, then format with `strerror`. The project does not currently have a thread-safe wrapper; new code preferring `strerror_r` is welcome:

```c
fd = open(path, O_RDONLY);
if (fd < 0) {
    int err = errno;
    debug(D_WARNING, "open '%s' failed: %s", path, strerror(err));
    return -1;
}
```

### Logging layer

The "log and propagate" layer is, by convention, the top of an externally-triggered operation: the per-iteration entry points in `pdiff.c`, `pupload.c`, `pdownload.c`, `pfs.c`, the FUSE op handlers, and similar request dispatchers. Leaf helpers in `papi.c`, `pssl.c`, `pcrypto.c`, `pnetlibs.c` should *not* log on every error path — let the caller log with the context it has. The result is fewer duplicate lines per failure.

---

## §12 — Build flags, warnings, sanitizers

### Current state

The Linux build uses (from the top-level `Makefile`):

```
-DP_OS_LINUX
-D_FILE_OFFSET_BITS=64
-D_LARGEFILE64_SOURCE
-Wall
-Wpointer-arith
-O2
-g
-fno-stack-protector            # explicit; perf-driven
-fPIC
-std=gnu99
-fomit-frame-pointer            # x86 only
-mtune=core2                    # x86 only
```

CI (in `.gitlab/gitlab-ci.yaml`) adds **AddressSanitizer** to the unit-test job via `EXTRA_CFLAGS=-fsanitize=address -fno-omit-frame-pointer` and `EXTRA_LDFLAGS=-fsanitize=address`. ASan is run on Linux and macOS. **UBSan and TSan are not currently in CI** (UBSan addition is in-flight).

`-Werror` is **not** set. `-Wextra`, `-Wpedantic`, `-Wshadow`, `-Wsign-compare`, `-Wstrict-prototypes`, `-Wmissing-prototypes`, `-Wcast-align`, `-Wundef`, `-Wvla`, `-Wdouble-promotion` are **not** set.

### Mandatory rules

1. **Do not introduce `#pragma GCC diagnostic ignored` or `#pragma warning(disable: ...)` to silence warnings.** Warnings are fixed at the source. The only acceptable exception is silencing a warning in a third-party header included by the project; the suppression is scoped (`push`/`pop`) around the include and commented.
2. **Do not raise the warning floor on a per-file basis.** If a new warning class would be useful project-wide, propose adding it to the top-level `Makefile`'s CFLAGS.
3. **Sanitizer findings are fixed, not suppressed.** Adding entries to a sanitizer suppression file requires human review and a comment naming the underlying bug or third-party limitation.
4. **No `__DATE__`, no `__TIME__`** in source code. Version strings come from the build system (`gitcommit.h`, `psettings.h`).
5. **Compiler-attribute annotations go through `pcompiler.h` macros** (§5). Raw `__attribute__` / `__declspec` outside `pcompiler.h` is not allowed.

### Aspirational additions (not currently in the build)

These are good additions that the project has not yet adopted; new code should be written assuming they will be enabled and should not introduce new violations of them. Do not enable them in the Makefile without coordinating with maintainers:

- **`-Wextra`**, **`-Wshadow`**, **`-Wstrict-prototypes`**, **`-Wmissing-prototypes`**, **`-Wcast-align`**, **`-Wformat=2`**, **`-Wformat-security`**, **`-Wundef`**, **`-Wvla`** — recommended additions to the warning floor.
- **`-Werror=implicit-function-declaration`**, **`-Werror=incompatible-pointer-types`**, **`-Werror=int-conversion`** — already partially in the test build (the tests' Makefile suppresses these via `-Wno-error=int-conversion -Wno-error=incompatible-pointer-types`, indicating GCC promotes them to errors at some baseline; resolving the underlying type mismatches and removing the suppressions is on the to-do list).
- **`-fstack-protector-strong`** — no technical blocker. The current `-fno-stack-protector` is a perf optimization paired with `-fomit-frame-pointer`; enabling stack-protector-strong would add canaries only on functions with buffers or that take address-of-local. Modest cost, real defense against stack-buffer-overflow exploits.
- **`-D_FORTIFY_SOURCE=2`** (or `=3` on glibc 2.34+) — replaces unsafe libc calls with bounds-checked versions when destination size is known to the compiler. Negligible cost, requires `-O1` or higher (already `-O2`).
- **UndefinedBehaviorSanitizer (UBSan)** in CI — addition is in-flight. Use `-fsanitize=undefined -fno-sanitize-recover=undefined`.
- **ThreadSanitizer (TSan)** in CI — useful given the shared-state threading model. Runs as a separate CI job from ASan (mutually exclusive).
- **Reproducible builds** — embed version via `-DPROJECT_VERSION="..."`, normalize debug-info paths via `-ffile-prefix-map=`, use `ar Drc` for deterministic archives.
- **`-Werror`** for CI — opt-in per build configuration. Local builds may warn-only.

### Debug vs. release

The project does not currently maintain separate Debug / Release / RelWithDebInfo configurations. The single Linux build uses `-O2 -g`. Setting `-DDEBUG_LEVEL=...` at compile time changes the verbosity of `debug(...)` calls (and enables the `0xfa` poison fill in `psync_malloc` via `IS_DEBUG`). For sanitizer runs, CI rebuilds with `EXTRA_CFLAGS=-fsanitize=...`.

When asserts/`assert` macros from `pcore.h` are needed at full strength, set `DEBUG_LEVEL=D_WARNING` or higher at compile time. Below that, both `assert` and `assertw` compile out.

### Minimum compiler versions

**Not formally pinned.** The codebase uses GCC builtins (`__sync_*`, `__builtin_expect`, `__builtin_prefetch`, `__has_attribute`, `__has_builtin`) that are universally available on any GCC ≥ 4.6 / Clang ≥ 3.0 — the practical floor is much lower than C11. Do not use C11- or later-only features (`<stdatomic.h>`, `_Thread_local`, anonymous unions in C99 mode without GCC extensions, etc.) without coordinating with maintainers.

When introducing a feature that requires a newer compiler version, document it.

---

## §13 — Testability

### What the agent produces

When the agent writes a new function in scope of the project's testable surface, it produces tests for that function in the same change. Tests are not deferred. Bug fixes are accompanied by regression tests that fail before the fix and pass after.

If a new function is genuinely untestable as written — heavy I/O coupling, hidden global state, deep dependency chains — the agent flags this and proposes a refactor before writing the function rather than producing untestable code.

### Testability properties

The agent writes code with these properties, in priority order:

1. **Explicit dependencies.** Functions take what they need as arguments. State that is shared across calls is held in a context struct passed in, not in module-level globals or singletons. Module globals are permitted only for genuinely process-global state and are documented at the declaration. (Existing pclsync uses module-level globals more freely than this — preserving that is fine; new code should prefer explicit context.)
2. **Separation of pure logic from I/O.** Parsing, formatting, validation, and computation are written as functions with no I/O side effects, fed by callers that handle I/O.
3. **Mockability via `fff.h`.** Modules whose tests need to substitute external dependencies (allocator, syscalls, API client) declare fakes in the test file using `FAKE_VALUE_FUNC` / `FAKE_VOID_FUNC`. See `tests/CLAUDE.md` for patterns.
4. **No hidden control flow.** No `setjmp`/`longjmp` for normal control flow. Signal handlers limited to setting `sig_atomic_t` flags. No `atexit` for application logic.

### Test framework and conventions

The project uses **Check** as its test framework, with **`fff.h`** (Fake Function Framework) for mocks. The **canonical reference is `tests/CLAUDE.md`** — read it before writing tests. The rules in this section are *additive* to that reference, not a replacement.

Key facts (cross-reference `tests/CLAUDE.md` for the full ruleset):

- Tests live under `tests/` (parallel to `pclsync/`'s flat layout — `tests/check_<module>.c`).
- Each test compiles its module-under-test's `.c` file directly via the `tests/Makefile`, no library linking.
- A test-local shim `tests/pcompat.h` provides minimal type stubs so modules can compile without the full platform layer.
- The Makefile auto-discovers `check_*.c` via wildcard but each test binary needs an explicit build rule listing which `.c` files to compile in.
- Run from the project root: `make check USESSL=openssl3` (the `USESSL` flag is needed only because the root Makefile builds the full library first; the `tests/Makefile` does not use `USESSL`).

### Mocking

For new code, mocking happens via:

1. **`fff.h` fakes** — declare with `FAKE_VALUE_FUNC` / `FAKE_VOID_FUNC`, reset in a `tcase_add_checked_fixture` setup, configure with `.return_val`, `SET_RETURN_SEQ`, or `.custom_fake`.
2. **Link-time substitution** — link the test against a stub `.c` file instead of the real one. The `tests/Makefile` supports this; use it where a whole module needs replacement (`tests/papi_stubs.c`, `tests/pssl_test_stubs.c`, `tests/stubs.c` are existing examples).

Forbidden:

- Weak symbols (`__attribute__((weak))`). Not portable to MSVC.
- `LD_PRELOAD` / interpose mechanisms. Not portable, runtime overhead.
- Preprocessor substitution (`#define foo test_foo` before include). Brittle.

### Coverage expectations

1. Every new public function has at least one happy-path test and at least one error-path test.
2. Every documented error return is reachable from some test. Coverage measured behaviorally.
3. Non-trivial parsers and state machines are tested with table-driven tests (input → expected output pairs).
4. Concurrency primitives (queues, lock-free structures, anything implementing the §10 locking discipline at a non-trivial level) get stress tests; aim to run them under TSan once TSan lands in CI.
5. Bug fixes carry regression tests.

### Test hygiene

1. Tests pass under every sanitizer enabled in CI (currently ASan).
2. Tests run on every supported platform. Platform-specific tests are gated by build-system feature probes, not `#ifdef` in the test source.
3. Tests do not depend on network access, on filesystem state outside their own temp directory, or on wall-clock time. Time-dependent code uses an injected clock.
4. Tests do not leak state to other tests. Check's per-test-case process isolation handles most of this, but tests must not assume it.
5. Tests do not depend on test execution order.
