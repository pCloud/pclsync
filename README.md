# pCloud Sync Library

A C library for pCloud file synchronization, providing a virtual filesystem, binary-protocol API client, bidirectional sync engine, and end-to-end encryption.

## Features

- **Bidirectional file sync** — local-to-remote and remote-to-local synchronization with conflict handling
- **Virtual filesystem** — FUSE-based mountable filesystem with page cache and on-disk caching
- **End-to-end encryption** — AES-256-CTR + HMAC-SHA512 per-file encryption, RSA-4096 key exchange
- **Multiple SSL backends** — wolfSSL, OpenSSL 1.1.x, OpenSSL 3, mbedTLS, SecureTransport
- **Cross-platform** — Linux, macOS, Windows
- **Binary protocol** — efficient binary API client for pCloud services

## Requirements

| Dependency | Purpose |
|---|---|
| `libsqlite3` | Persistent state database |
| `libfuse` / `libosxfuse` | Virtual filesystem (FUSE builds only) |
| `libpthread` | Threading |
| `zlib` | Compression (also bundled as `miniz.c`) |
| SSL library | One of: wolfSSL, OpenSSL, mbedTLS, SecureTransport |

Compiler: GCC or compatible, C99 (`-std=gnu99`).

## Building

The build system uses GNU Make.

```sh
make all              # build psynclib.a (static library, requires USESSL=)
make fs               # build with FUSE filesystem support
make shared           # build shared library (libpsynclib.so / .dylib)
make shared-fs        # build shared library with FUSE support
make cli              # build the CLI test tool (includes FUSE)
make overlay_client   # build overlay icons client (Linux)
make clean            # remove build artifacts
```

### SSL backend selection

When `USESSL` is omitted, the build system auto-detects the best available SSL provider via `pkg-config` (trying OpenSSL, then wolfSSL, then mbedTLS; on macOS it falls back to SecureTransport). You can override auto-detection by passing `USESSL=` explicitly:

| Value | Backend |
|---|---|
| `openssl` | OpenSSL 1.1.x |
| `openssl3` | OpenSSL 3.x |
| `wolfssl` | wolfSSL |
| `mbed` | mbedTLS 1.3.x (PolarSSL API) |
| `securetransport` | SecureTransport (macOS only) |

Example:

```sh
make fs USESSL=openssl
make all USESSL=mbed
```

### Dependency discovery

Dependencies are discovered via `pkg-config` by default. If `pkg-config` is not available or a library is installed in a non-standard location, use the `*_INCLUDE_DIR` override variables listed below.

### Build variables

| Variable                 | Default       | Purpose                                    |
|--------------------------|---------------|--------------------------------------------|
| `USESSL`                 | *(auto-detected)* | SSL backend                            |
| `SQLITE_INCLUDE_DIR`     | *(pkg-config)* | Path to SQLite headers                    |
| `FUSE_INCLUDE_DIR`       | *(pkg-config)* | Path to FUSE headers                      |
| `OPENSSL_INCLUDE_DIR`    | *(pkg-config)* | Path to OpenSSL headers                   |
| `MBEDTLS_INCLUDE_DIR`    | *(pkg-config)* | Path to mbedTLS headers                   |
| `WOLFSSL_INCLUDE_DIR`    | *(pkg-config)* | Path to wolfSSL headers                   |
| `GCC_OPTIMIZATION_LEVEL` | `s`           | GCC `-O` level (macOS)                     |
| `DEBUG_LEVEL`            | *(unset)*     | Debug verbosity: 0 (trace) to 4 (critical) |

### Installing dependencies

**Debian/Ubuntu:**
```sh
apt install libsqlite3-dev libfuse-dev zlib1g-dev
# Plus one SSL provider:
apt install libssl-dev          # OpenSSL
apt install libwolfssl-dev      # WolfSSL
apt install libmbedtls-dev      # mbedTLS 1.3.x (PolarSSL API) — see note below
```

**Fedora/RHEL:**
```sh
dnf install sqlite-devel fuse-devel zlib-devel
dnf install openssl-devel       # or wolfssl-devel / mbedtls-devel
```

> **Note:** The `mbed` SSL backend targets PolarSSL / mbedTLS 1.3.x, the last version that used the `polarssl/` header path and PolarSSL symbol names. Modern distro packages (`libmbedtls-dev` on Debian/Ubuntu, `mbedtls-devel` on Fedora) ship mbedTLS 2.x or 3.x, which are **not compatible** with this backend. If you need mbedTLS, you must build PolarSSL 1.3.x from source. For most users, OpenSSL or wolfSSL are the recommended choices.

**macOS (Homebrew):**
```sh
brew install sqlite3 macfuse
brew install openssl@3          # or wolfssl / mbedtls
```

## API Overview

The public API is declared in `psynclib.h`. All exported symbols use the `psync_` prefix. Functions return `0` on success and `-1` on failure unless otherwise noted. All paths are UTF-8.

Key API categories:

- **Lifecycle** — `psync_init()`, `psync_start_sync()`, `psync_destroy()`, `psync_pause()`, `psync_resume()`, `psync_stop()`
- **Authentication** — `psync_set_user_pass()`, `psync_set_auth()`, `psync_logout()`, `psync_unlink()`, `psync_tfa_*()`
- **Sync management** — `psync_add_sync_by_path_delayed()`, `psync_change_synctype()`, `psync_delete_sync()`, `psync_get_status()`, `psync_run_localscan()`
- **Virtual filesystem** — `psync_fs_start()`, `psync_fs_stop()`, `psync_fs_getmountpoint()`
- **pCloud Encryption (Crypto Folder)** — `psync_crypto_setup()`, `psync_crypto_start()`, `psync_crypto_stop()`, `psync_crypto_mkdir()`
- **Folder Invites & Shares** — `psync_share_folder()`, `psync_accept_share_request()`, `psync_remove_share()`
- **Public/upload links** — `psync_delete_link()`, `psync_change_link()`, `psync_change_upload_link()`
- **Settings** — `psync_get_bool_setting()`, `psync_set_bool_setting()`, `psync_set_string_setting()`
- **Contacts & teams** — `psync_list_contacts()`, `psync_list_myteams()`, `psync_account_teamshare()`
- **File operations** — `psync_upload_data()`, `psync_upload_file()`, `psync_create_remote_folder()`

## Architecture

```
PUBLIC API  (psynclib.h)
      |
SYNC ENGINE  (psyncer.c / pdiff.c / plocalscan.c)
      |
 +----+----+
UPLOAD     DOWNLOAD
(pupload)  (pdownload)
      |
NETWORK LAYER  (pnetlibs.c / pasyncnet.c / papi.c)
      |
VIRTUAL FS  (pfs.c / ppagecache.c / pfstasks.c / pfsupload.c)
      |
ENCRYPTION  (pcrypto.c / pfscrypto.c / pssl.c)
      |
DATABASE  (SQLite -- schema in pdatabase.h)
```

## Testing

Tests live in `tests/` and use the [Check](https://libcheck.github.io/check/) unit testing framework with [FFF](https://github.com/meekrosoft/fff) for mocking. Test build configuration is in `tests/Makefile`.

### Requirements

Install the Check testing framework:

```sh
# Fedora/RHEL
sudo dnf install check check-devel

# Debian/Ubuntu
sudo apt-get install check

# macOS
brew install check
```

### Core tests

```sh
make check                 # build + run all core tests (plist, ptree, papi)
make test                  # alias for make check
```

### SSL provider tests

Each SSL backend has its own test suite. Run them from `tests/`:

```sh
cd tests
make check-pssl-openssl    # OpenSSL 1.1.x tests
make check-pssl-openssl3   # OpenSSL 3.x tests
make check-pssl-wolfssl    # wolfSSL tests
make check-pssl-mbedtls    # mbedTLS tests
make check-pssl            # run all SSL provider tests
```

### Network integration tests

These tests perform real SSL connections and API calls against pCloud servers:

```sh
cd tests
make check-network-openssl3   # OpenSSL 3.x network tests
make check-network             # run all network provider tests
```

### Running a single test

```sh
cd tests
make build/check_ptree     # build one test binary
./build/check_ptree        # run it
```

Exit code is `0` on all-pass, `1` on any failure.

## Platform Support

| Platform | FS backend                                       | Notes                                        |
|----------|--------------------------------------------------|----------------------------------------------|
| Linux    | libfuse                                          | Overlay icons via D-Bus                      |
| macOS    | libosxfuse                                       | SecureTransport available; Cocoa integration |
| Windows  | FUSE-compatible driver/layer (WinFsp, cbfs, ...) | COM overlay icons                            |

Platform-specific code lives in `pcompat.c`.

## License

See [LICENSE](LICENSE) for details.
