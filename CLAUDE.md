# pclsync C Library

## Overview

pclsync is a C library (~96K LOC) that implements the pCloud sync engine: cloud file synchronization, FUSE virtual filesystem, and client-side AES-256/RSA encryption.

**License:** BSD 3-Clause (Anton Titov / pCloud Ltd)

## Build System

- **Compiler:** GCC, GNU C99 (`-std=gnu99`)
- **Output:** `psynclib.a` (static library via `ar rcu` + `ranlib`)
- **Build dir:** `build/`

### Make Targets

| Target | Description |
|--------|-------------|
| `make all` | Build library without FUSE support |
| `make fs` | Build library with FUSE support |
| `make cli` | Build standalone CLI executable |
| `make check` / `make test` | Run unit tests |
| `make clean` | Remove build artifacts |
| `make install` | Install library and headers |

### SSL Backend Selection

Set `USESSL=<backend>` when invoking make:

| Value | Define | Backend file |
|-------|--------|-------------|
| `openssl` | `P_SSL_OPENSSL` | `pssl-openssl.c` |
| `mbed` | `P_SSL_MBEDTLS` | `pssl-mbedtls.c` |
| `wolfssl` | `P_SSL_WOLFSSL` | `pssl-wolfssl.c` |
| `securetransport` | `P_SSL_SECURETRANSPORT` | `pssl-securetransport.c` |

### Dependencies

- **Linux:** libfuse, pthread, sqlite3, zlib, openssl
- **macOS:** macFUSE, sqlite3, OpenSSL, Cocoa framework

## Testing

- **Framework:** [Check](https://libcheck.github.io/check/) (C unit testing)
- **Mocking:** `fff.h` (Fake Function Framework)
- **Location:** `tests/`
- **Run:** `make check USESSL=openssl` from this directory (the `tests/Makefile` itself does not use `USESSL`; the flag is only needed at the root level to build the library first)
- **Test pattern:** Each test file compiles the module under test directly (e.g., `check_plist.c` compiles with `../plist.c`)
- **Shims:** `tests/pcompat.h` provides minimal type stubs for isolated module testing

## Architecture

### Key Subsystems

| File(s) | Subsystem |
|---------|-----------|
| `psynclib.c/h` | Public API: init, auth, sync, status, callbacks |
| `pcompat.c/h` | Platform abstraction (file I/O, sockets, threads, time) |
| `pssl.c/h` + backends | SSL/TLS and crypto abstraction |
| `pfs.c/h`, `pfsfolder.c/h`, `pfsstatic.c/h` | FUSE filesystem implementation |
| `pfscrypto.c/h` | Encrypted filesystem layer |
| `pcloudcrypto.c/h` | Client-side encryption (AES-256/RSA) |
| `pdiff.c/h` | Remote change diffing and application |
| `pdownload.c/h`, `pupload.c/h` | File transfer |
| `plocalscan.c/h` | Local filesystem monitoring |
| `psyncer.c/h` | Sync orchestration |
| `pcache.c/h`, `ppagecache.c/h` | Caching layers |
| `pnetlibs.c/h` | Network utilities and API communication |
| `papi.c/h` | Binary API protocol (custom serialization) |
| `ptasks.c/h` | Background task management |
| `pstatus.c/h` | Status tracking and reporting |
| `plocks.c/h` | Custom rwlock with read-starve-writer, timed locks |
| `plist.c/h` | Linux kernel-style intrusive doubly-linked list |
| `ptree.c/h` | Tree data structures |
| `pintervaltree.c/h` | Interval tree |
| `miniz.c/h` | Embedded zlib-compatible compression |

### Database

SQLite3 with WAL journal mode, 20+ tables for folders, files, sync state, crypto keys, tasks, page cache, shares. Config: `page_size=4096, synchronous=NORMAL, locking_mode=EXCLUSIVE, cache_size=8000`.

### Threading Model

pthread-based. Library spawns internal threads for sync, download, upload, scanning, and diff processing. Callbacks may fire from any internal thread.

### Binary API Protocol

Custom binary serialization via `binparam`/`binresult` structures. Parameter types: `STR`, `NUM`, `BOOL`, `ARRAY`, `HASH`, `DATA`. Defined in `papi.h`.

## Coding Conventions

### Naming

- Public symbols: `psync_` prefix (e.g., `psync_init()`, `psync_malloc()`)
- Types: `_t` suffix (e.g., `psync_folderid_t`, `psync_socket_t`, `pstatus_t`)
- Internal types: `p` prefix (e.g., `pentry_t`, `pfolder_t`)
- Constants/errors: `PSYNC_*`, `PERROR_*`, `PSTATUS_*`, `D_*` (debug levels)

### Style

- **Indentation:** 2 spaces (predominant; some files use tabs)
- **Braces:** K&R style, opening brace on same line
- **Spacing:** Often no space around `=` or before `{` in function definitions
- **Comments:** `/* */` for file headers and API docs; `//` for inline notes
- **Include guards:** `#ifndef _PSYNC_*_H` / `#define _PSYNC_*_H`
- **File headers:** BSD 3-clause license block at top of every file

### Compiler Attributes (pcompiler.h)

`PSYNC_NOINLINE`, `PSYNC_MALLOC`, `PSYNC_PURE`, `PSYNC_CONST`, `PSYNC_COLD`, `PSYNC_FORMAT`, `PSYNC_NONNULL`, `PSYNC_PACKED_STRUCT`, `likely()`/`unlikely()`, `PSYNC_THREAD` (thread-local storage)

### Debug Macros

```c
debug(level, "format string", args...);  // wraps psync_debug() with file/func/line
```

Debug levels: `D_NONE(0)`, `D_BUG(10)`, `D_CRITICAL(20)`, `D_ERROR(30)`, `D_WARNING(40)`, `D_NOTICE(50)`

### Memory Management

- Custom allocator support via `psync_set_alloc()`
- Convenience macros: `psync_new(type)`, `psync_new_cnt(type, cnt)`
- Emergency malloc retries after freeing caches; aborts on final OOM
- Debug mode fills allocations with `0xfa`

### Error Handling

- Functions return `0` for success, `-1` for failure
- Thread-local `psync_error` holds last error code (`PERROR_*` constants)

## Public API (psynclib.h)

### Lifecycle

`psync_init()` -> `psync_start_sync()` -> `psync_stop()` / `psync_destroy()`

### Key API Groups

- **Auth:** `psync_set_user_pass()`, `psync_set_auth()`, `psync_logout()`, `psync_unlink()`
- **TFA:** `psync_tfa_has_devices()`, `psync_tfa_type()`, `psync_tfa_send_sms()`, `psync_tfa_set_code()`
- **Web login:** `psync_get_login_req_id()`, `psync_wait_auth_token()`, `psync_wait_auth_token_async()`
- **Status:** `psync_get_status()`, `psync_get_last_error()`, `psync_download_state()`
- **Sync:** `psync_add_sync_by_path()`, `psync_delete_sync()`, `psync_get_sync_list()`
- **FUSE:** `psync_fs_start()`, `psync_fs_stop()`, `psync_fs_getmountpoint()`
- **Crypto:** `psync_crypto_setup()`, `psync_crypto_start()`, `psync_crypto_stop()`, `psync_crypto_mkdir()`
- **Settings:** Typed getters/setters for bool/int/uint/string
- **Callbacks:** `pstatus_change_callback_t`, `pevent_callback_t`, `pnotification_callback_t`

### Key Types

- `psync_folderid_t`, `psync_fileid_t` — `uint64_t`
- `psync_syncid_t`, `psync_eventtype_t` — `uint32_t`
- `pstatus_t` — status struct passed to `psync_get_status()`
- 21 status codes: `PSTATUS_READY(0)` through `PSTATUS_RELOCATED(20)`

## Platform Support

| Platform | Define | Notes |
|----------|--------|-------|
| Linux | `P_OS_LINUX` | Primary target, FUSE 2.x |
| macOS | `P_OS_MACOSX` | macFUSE required |
| Windows | `P_OS_WINDOWS` | Builds as DLL, limited support |
| BSD | `P_OS_BSD` | Basic support |

Platform detection is automatic via compiler macros in `pcompat.h`. All POSIX systems share `P_OS_POSIX`.

## Git Commits

- **Always** follow the commit message conventions defined in `COMMIT.md`.
- Use the `/commit` slash command to create commits — it automates the full workflow (diff review, category selection, message drafting, user approval).
- Format: `{Category} | {Imperative verb} {what changed}` (max 80 chars, no trailing period).
- Categories: `CLI`, `Auth`, `Crypto`, `Mount`, `Sync`, `Daemon`, `FFI`, `Build`, `Deps`, `CI`, `Docs`.
- Do **not** append `Co-Authored-By:` or any similar footer.
- Do **not** amend previous commits unless explicitly asked.
- Do **not** push to remote unless explicitly asked.
