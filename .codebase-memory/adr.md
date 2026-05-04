# Architecture Decision Record — pclsync

## 1. Project Identity & Scope

`pclsync` is a ~96K-LOC **C99 static library** (`psynclib.a`) that implements the pCloud sync engine. It is BSD-3-clause licensed (Anton Titov / pCloud Ltd) and is the shared core consumed by the pCloud desktop client, the standalone `cli` executable in this repo, and FFI bindings.

The repository ships three deliverables:
- `psynclib.a` — the static library (`make all` without FUSE, `make fs` with FUSE)
- `cli` — a standalone daemon + control binary (`make cli`)
- `tests/check_*` + `integration-tests/` — Check/fff unit tests and a Python pytest integration suite

The graph index covers **228 modules, 2723 functions, 5021 nodes, 15656 edges** — most of the C code is module-flat at the repository root (`psynclib.c`, `pdiff.c`, `pfs.c`, etc.); only `lib/poverlay*` (Windows/Linux shell-overlay helpers), `tests/`, `integration-tests/`, and `docs/wiki/` are nested.

## 2. Top-Level Architecture

The library is organized as a **layered set of modules with a single public façade** (`psynclib.h`). All public symbols carry the `psync_` prefix; internal types use `p` prefix and `_t` suffix.

Layers, bottom up:

1. **Platform & primitives** — `pcompat` (POSIX/Win abstraction), `pcompiler.h` (attribute macros), `plist`/`ptree`/`pintervaltree` (intrusive containers), `plocks` (custom rwlock w/ read-starve-writer + timed locks), `ptimer`, `pmem`/`pmemlock`, `pqsort`, `prunratelimit`, `miniz` (embedded zlib), `pcrc32c`.
2. **I/O & crypto primitives** — `pssl` + backend `pssl-{openssl,openssl3,mbedtls,wolfssl,securetransport}.c` (transport TLS), `pcrypto` (low-level), `papi` (custom binary protocol with `binparam`/`binresult`, types STR/NUM/BOOL/ARRAY/HASH/DATA), `pnetlibs`, `pasyncnet`, `pp2p`.
3. **Persistence** — `psql` + `plibs` wrap a single SQLite3 database (WAL journal, `synchronous=NORMAL`, `locking_mode=EXCLUSIVE`, `page_size=4096`, `cache_size=8000`, **`SQLITE_CONFIG_SERIALIZED` threading**). 20+ tables for folders, files, sync state, crypto keys, tasks, page cache, shares.
4. **Domain** — `pcloudcrypto` (account-level AES-256/RSA), `pfolder`/`pfileops`, `ppathstatus`, `pdevicemap`, `pbusinessaccount`, `pcontacts`, `publiclinks`, `ppassword`, `pdevice_monitor`, `pscanner`.
5. **Sync engine** — `psyncer` (orchestration), `pdiff` (remote-change diff/apply), `plocalscan` + `plocalnotify` (local FS watching), `pdownload`/`pupload` (transfers), `ptasks` (background task queue), `pstatus`/`pexternalstatus`/`pcallbacks`/`pnotifications` (state propagation), `pfsupload`, `pfstasks`.
6. **FUSE virtual filesystem** — `pfs` (real impl, FUSE 2.x / macFUSE), `pfsfolder`, `pfsstatic`, `pfscrypto` (encrypted FS layer), `pfsxattr`, `pcache` + `ppagecache` (block-level page cache). `pfsfake.c` is a build-time stub used when FUSE is disabled.
7. **Public façade** — `psynclib.c/h` exposes lifecycle (`psync_init` → `psync_start_sync` → `psync_stop` → `psync_destroy`), auth, TFA, web-login, status, sync, FUSE, crypto, settings, callbacks. `pcore.h`/`ptypes.h`/`psettings.h` carry shared types and tunables.
8. **Shell integration & CLI** — `poverlay.c` (shared overlay protocol) plus per-OS `poverlay_lin.c` / `poverlay_mac.c` / `poverlay_win.c`; `lib/poverlay/` is a Windows COM shell-extension project; `lib/poverlay_linux/` is a small overlay client. The `cli.c` binary embeds the library and exposes subcommands (`start`, `stop`, `status`, `sync add|list|delete`); subcommands beyond `start` talk to a running engine over a Unix domain socket at `<datadir>/cli.sock` via `cli_ipc.c`.

## 3. Key Architectural Decisions

### 3.1 Static library + custom build
- **Decision:** Hand-written GNU `Makefile`, `gcc -std=gnu99`, output via `ar rcu` + `ranlib`. No CMake, no autotools, no pkg-config of own headers.
- **Why:** Predictable cross-platform builds; the library is consumed as a sub-build by external clients. Build dir is `build/`.
- **Consequence:** Adding a source file means editing `Makefile`; there is no automatic glob.

### 3.2 Compile-time SSL backend selection
- **Decision:** Backend chosen by `USESSL={openssl|openssl3|mbed|wolfssl|securetransport}` Makefile variable; defines `P_SSL_*`; selects exactly one of the `pssl-*.c` files implementing the common `pssl.h` interface.
- **Why:** Different OS/distro shipping requirements (macOS may use SecureTransport, Linux distros may pin OpenSSL 1.1 vs 3, embedded targets use mbedTLS/wolfSSL).
- **Consequence:** Any `pssl.h` change must be mirrored across all backend files; tests have a per-backend variant (`check_pssl_openssl.c`, `check_pssl_openssl3.c`, `check_pssl_mbedtls.c`, `check_pssl_wolfssl.c`).

### 3.3 Platform abstraction via `pcompat`
- **Decision:** `pcompat.c/h` is the **only** layer permitted to use raw POSIX / Win32 APIs for file I/O, sockets, threads, and time. `P_OS_LINUX` / `P_OS_MACOSX` / `P_OS_WINDOWS` / `P_OS_BSD` / `P_OS_POSIX` are auto-detected.
- **Why:** Single seam keeps the rest of the codebase portable.
- **Consequence:** New syscalls go in `pcompat`. Tests stub it via `tests/pcompat.h` to compile modules in isolation.

### 3.4 SQLite as the only durable store, configured for safety over throughput
- **Decision:** Single SQLite3 database file in `<datadir>`, WAL mode, EXCLUSIVE locking, **serialized threading** (`SQLITE_CONFIG_SERIALIZED`, set in commit `fb784f9` to prevent SQL parser races).
- **Why:** Embedded reliability, no external server, atomic transactions for diff application; serialized threading was needed because the engine fans out heavy concurrency over a single connection.
- **Consequence:** Lock contention is a real risk — see `db-lock-network-deadlock-audit.md` for the documented audit. Network operations must never be called while holding the SQL lock.

### 3.5 Threading model: pthread + custom rwlock
- **Decision:** All concurrency is pthread-based. Internal threads run for diff, sync, download, upload, local scan, FUSE, callbacks. `plocks.c/h` provides a custom rwlock with read-starve-writer ordering and timed acquisitions. Errors are stored in a thread-local `psync_error` (`PSYNC_THREAD` storage class).
- **Why:** Status callbacks fire from arbitrary internal threads; consumers must be reentrant. The custom rwlock matches the engine's preference for prompt writer progress.
- **Consequence:** Public callbacks (`pstatus_change_callback_t`, `pevent_callback_t`, `pnotification_callback_t`) document that they may fire from any thread; consumers must marshal to their own loop.

### 3.6 Pluggable allocator and OOM strategy
- **Decision:** `psync_set_alloc()` injects `malloc`/`realloc`/`free`. Convenience macros `psync_new(type)` / `psync_new_cnt(type, cnt)`. On OOM, the engine **frees caches and retries**; only after that final retry fails does it abort. Debug builds fill allocations with `0xfa`.
- **Why:** Long-running daemon must survive transient memory pressure without crashing user sessions.
- **Consequence:** Cache subsystems (`pcache`, `ppagecache`) must expose a safe drop-everything path.

### 3.7 Custom binary API protocol
- **Decision:** All cloud RPC traffic uses the proprietary binary protocol implemented by `papi.c/h` over TLS, with `binparam` / `binresult` structures and the type set `{STR, NUM, BOOL, ARRAY, HASH, DATA}`.
- **Why:** Tighter than HTTP/JSON for the protocol's hot path; long-standing pCloud server contract.
- **Consequence:** New endpoints require both client (`papi.h`) and server-side coordination; do not add HTTP/REST inside the library.

### 3.8 Layered crypto: transport vs. account vs. filesystem
- **Decision:** Three independent crypto modules:
  - `pssl` — transport TLS (backend-pluggable, see 3.2).
  - `pcrypto` — low-level primitives (hashing, AES, etc.).
  - `pcloudcrypto` — account-scoped client-side AES-256/RSA, key derivation, key storage.
  - `pfscrypto` — encrypted-folder layer wrapping FUSE I/O.
- **Why:** Threat-model isolation: transport, at-rest account secrets, and per-folder filesystem encryption are different security domains with different lifetimes.
- **Consequence:** Do not mix layers. New folder-level encryption goes in `pfscrypto`; new account-level features in `pcloudcrypto`.

### 3.9 FUSE optionality via build target swap
- **Decision:** `make all` links `pfsfake.c` (stub `psync_fs_*` symbols). `make fs` links the real `pfs.c` plus `pfsfolder`, `pfsstatic`, `pfscrypto`, `pfsxattr`, page cache. Same public symbols either way.
- **Why:** Headless / server / mobile builds don't need the FUSE dependency; the symbol shape stays stable for callers.
- **Consequence:** Any `pfs.c` change must be matched with a no-op stub in `pfsfake.c` if it adds a new public symbol.

### 3.10 CLI as a daemon with Unix-socket IPC
- **Decision:** `cli start` launches the engine in-process; other subcommands (`stop`, `status`, `sync add|list|delete`) are short-lived clients that connect to `<datadir>/cli.sock` and send IPC requests handled by `cli_ipc.c`.
- **Why:** Lets operators inspect / control a running daemon without extra dependencies.
- **Consequence:** New CLI subcommands need both a router entry in `cli.c` and a request/response handler in `cli_ipc.c`.

### 3.11 Testing strategy: per-module recompilation, no library link
- **Decision:** Each Check test (`tests/check_<module>.c`) **recompiles** the module under test directly (e.g., `check_plist.c` compiles with `../plist.c`) rather than linking `psynclib.a`. `fff.h` provides function fakes; `tests/pcompat.h` plus `tests/stubs.c` / `tests/papi_stubs.c` / `tests/pssl_test_stubs.c` provide minimal type / symbol stubs.
- **Why:** Lets a test exercise a module without dragging in the full engine; supports symbol-level mocking that a static-library link would forbid.
- **Consequence:** Module-internal helpers (`static`-stripped or otherwise visible) can be tested directly. Don't add cross-module includes that pull in the world; keep modules surface-stable.

### 3.12 Integration tests in Python (separate concern)
- **Decision:** `integration-tests/` is a Python pytest project (with its own `pytest.ini`, `ruff.toml`, `requirements`, helpers under `utils/`, scenario suites under `tests/sync/` and `tests/drive/`). It drives a built `cli` binary and a real backend.
- **Why:** End-to-end scenarios (sync correctness, conflict resolution, FUSE behavior, stress) are easier to express in Python than in Check.
- **Consequence:** The Python suite runs at CI level only; it is **not** part of `make check`. Three driver scripts at the repo root (`run-integration-{sync,drive,sync-single}.sh`) wire them up.

### 3.13 Status & callbacks as the public state surface
- **Decision:** Consumers don't poll internals — they register callbacks. `pstatus` aggregates state into a single `pstatus_t` struct returned by `psync_get_status()`; status codes range from `PSTATUS_READY(0)` to `PSTATUS_RELOCATED(20)` (21 codes). `pcallbacks.c` and `pnotifications.c` dispatch.
- **Why:** Decouples client UI from engine internals; lets the engine evolve without breaking integrations.
- **Consequence:** Adding a new status code is a public-API change — bump documentation and the wiki page (`docs/wiki/05-status-and-callbacks.md`).

## 4. Conventions That Tooling Must Respect

- **Naming:** Public `psync_` prefix; types end in `_t`; internal types prefixed `p`; constants `PSYNC_*`, `PERROR_*`, `PSTATUS_*`, `D_*`.
- **Style:** K&R braces; predominantly 2-space indent (some files use tabs — preserve per-file); no space around `=` or before `{` in function definitions; `/* */` for headers and API docs, `//` for inline notes; include guards `_PSYNC_*_H`; BSD-3-clause header on every source file.
- **Debug:** `debug(level, fmt, ...)` macro wraps `psync_debug()` and adds `__FILE__/__func__/__LINE__`. Levels: `D_NONE(0)`, `D_BUG(10)`, `D_CRITICAL(20)`, `D_ERROR(30)`, `D_WARNING(40)`, `D_NOTICE(50)`.
- **Error returns:** `0` = success, `-1` = failure; thread-local `psync_error` holds last `PERROR_*`.
- **Compiler attributes:** Use the macros in `pcompiler.h` (`PSYNC_NOINLINE`, `PSYNC_MALLOC`, `PSYNC_PURE`, `PSYNC_CONST`, `PSYNC_COLD`, `PSYNC_FORMAT`, `PSYNC_NONNULL`, `PSYNC_PACKED_STRUCT`, `likely`/`unlikely`, `PSYNC_THREAD`) — do not write raw `__attribute__((...))`.
- **Commits:** Follow `COMMIT.md`. Format `{Category} | {Imperative verb} {what changed}` (≤80 chars, no trailing period). Categories: `CLI`, `Auth`, `Crypto`, `Mount`, `Sync`, `Daemon`, `FFI`, `Build`, `Deps`, `CI`, `Docs`. No `Co-Authored-By` footer. Use `/commit` for the workflow.

## 5. Known Hot Spots & Gotchas

- **DB-lock × network deadlock:** Audited in `db-lock-network-deadlock-audit.md` at repo root. Never hold a SQL transaction across a network call.
- **SQLite serialized threading:** Recently switched (commit `fb784f9`) — relying on `SQLITE_CONFIG_SERIALIZED` is the current invariant; prior code assumed multi-thread mode.
- **Local-scan wake rate-limit:** Halved to 30 s in commit `6eaff69`; `plocalscan.c` cadence has shifted from older docs.
- **macOS / venv plumbing:** CI integration jobs honor `SKIP_VENV` (commit `03af1d3`); macOS jobs use venv.
- **CLI mount detection:** The drive mount path is resolved before grepping `mount` (commit `c1368d7`) — don't regress to raw path matching.
- **`docs/wiki/`** is the long-form companion documentation. Top-level CLAUDE.md is authoritative for build / convention / API summary.

## 6. Useful Pointers for Future Work

- **Adding a new public API:** edit `psynclib.h` + `psynclib.c`, add the wiki page under `docs/wiki/`, update `pstatus`/`pcallbacks` if it surfaces state.
- **Adding a new sync-engine task type:** wire it in `ptasks.c` and the matching consumer (`pdownload`/`pupload`/`pdiff`/`psyncer`).
- **Adding a new SSL backend:** new `pssl-<name>.c` implementing `pssl.h`, new `USESSL=` value in `Makefile`, new `tests/check_pssl_<name>.c` + `tests/check_network_<name>.c`.
- **Adding a CLI subcommand:** route in `cli.c`, request/response in `cli_ipc.c`, document in this file's §2 and in CLAUDE.md.
- **Indexing / graph queries:** project key is `home-georgi-neykov-CLionProjects-synclib`. Use `search_graph` / `trace_path` / `query_graph` before falling back to grep.
