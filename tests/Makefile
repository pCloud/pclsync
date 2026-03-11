CC = gcc
BUILD_DIR = build
PKG_CONFIG ?= pkg-config

# --- Check framework detection ---
CHECK_INSTALLED := $(shell $(PKG_CONFIG) --exists check 2>/dev/null && echo yes || echo no)

ifeq ($(CHECK_INSTALLED),no)
$(error Check testing framework not found. Install it:\
	Fedora/RHEL: sudo dnf install check check-devel\
	Debian/Ubuntu: sudo apt-get install check\
	macOS: brew install check\
	Docs: https://libcheck.github.io/check/web/install.html)
endif

CHECK_CFLAGS = $(shell $(PKG_CONFIG) --cflags check)
CHECK_LIBS   = $(shell $(PKG_CONFIG) --libs check)

CFLAGS = -I. -I.. -std=gnu99 -g -O0 -Wall -fPIC \
         -DDEBUG_LEVEL=0 -DP_OS_LINUX -D_FILE_OFFSET_BITS=64 \
         -Wno-error=int-conversion -Wno-error=incompatible-pointer-types \
         $(CHECK_CFLAGS)
LDFLAGS = $(CHECK_LIBS)

# Network tests need the real pcompat.h (with psync_socket etc.),
# so -I.. must come before -I. to avoid the test shim.
NET_CFLAGS = -I.. -I. -std=gnu99 -g -O0 -Wall -fPIC \
             -DDEBUG_LEVEL=0 -DP_OS_LINUX -D_FILE_OFFSET_BITS=64 \
             -Wno-error=int-conversion -Wno-error=incompatible-pointer-types \
             $(CHECK_CFLAGS)

# --- SSL library detection (mirrors top-level Makefile) ---
HAS_PKG_CONFIG := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && echo yes)
ifeq ($(HAS_PKG_CONFIG),yes)
  pkg_cflags = $(shell $(PKG_CONFIG) --cflags $(1) 2>/dev/null)
  pkg_libs   = $(shell $(PKG_CONFIG) --libs $(1) 2>/dev/null)
else
  pkg_cflags =
  pkg_libs   =
endif

# OpenSSL
ifdef OPENSSL_INCLUDE_DIR
  OPENSSL_CFLAGS = -I$(OPENSSL_INCLUDE_DIR)
else
  OPENSSL_CFLAGS := $(call pkg_cflags,openssl)
endif
OPENSSL_LIBS := $(call pkg_libs,openssl)
ifeq ($(OPENSSL_LIBS),)
  OPENSSL_LIBS = -lssl -lcrypto
endif

# mbedTLS
ifdef MBEDTLS_INCLUDE_DIR
  MBEDTLS_CFLAGS = -I$(MBEDTLS_INCLUDE_DIR)
else
  MBEDTLS_CFLAGS := $(call pkg_cflags,mbedtls)
endif
MBEDTLS_LIBS := $(call pkg_libs,mbedtls)
ifeq ($(MBEDTLS_LIBS),)
  MBEDTLS_LIBS = -lmbedtls -lmbedcrypto -lmbedx509
endif

# WolfSSL
ifdef WOLFSSL_INCLUDE_DIR
  WOLFSSL_CFLAGS = -I$(WOLFSSL_INCLUDE_DIR)
else
  WOLFSSL_CFLAGS := $(call pkg_cflags,wolfssl)
endif
WOLFSSL_LIBS := $(call pkg_libs,wolfssl)
ifeq ($(WOLFSSL_LIBS),)
  WOLFSSL_LIBS = -lwolfssl
endif

# SQLite3 (needed by some headers)
SQLITE_CFLAGS := $(call pkg_cflags,sqlite3)

$(BUILD_DIR):
	mkdir -p $@

# ══════════════════════════════════════════════════════════════════════════════
#  Core unit tests (no SSL provider dependency)
# ══════════════════════════════════════════════════════════════════════════════

$(BUILD_DIR)/check_plist: check_plist.c ../plist.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/check_ptree: check_ptree.c ../ptree.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/check_papi: check_papi.c ../papi.c papi_test_stubs.c pssl_test_stubs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SQLITE_CFLAGS) $^ -o $@ $(LDFLAGS)

CORE_BINS = $(BUILD_DIR)/check_plist $(BUILD_DIR)/check_ptree $(BUILD_DIR)/check_papi

# ══════════════════════════════════════════════════════════════════════════════
#  Per-provider SSL unit tests
# ══════════════════════════════════════════════════════════════════════════════

# --- OpenSSL ---
$(BUILD_DIR)/check_pssl_openssl: check_pssl_openssl.c ../pssl-openssl.c ../pssl.c pssl_test_stubs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DP_SSL_OPENSSL $(OPENSSL_CFLAGS) $^ -o $@ $(LDFLAGS) $(OPENSSL_LIBS) -lpthread

# --- OpenSSL 3 ---
$(BUILD_DIR)/check_pssl_openssl3: check_pssl_openssl3.c ../pssl-openssl3.c ../pssl.c pssl_test_stubs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DP_SSL_OPENSSL3 $(OPENSSL_CFLAGS) $^ -o $@ $(LDFLAGS) $(OPENSSL_LIBS) -lpthread

# --- WolfSSL ---
$(BUILD_DIR)/check_pssl_wolfssl: check_pssl_wolfssl.c ../pssl-wolfssl.c ../pssl.c pssl_test_stubs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DP_SSL_WOLFSSL $(WOLFSSL_CFLAGS) $^ -o $@ $(LDFLAGS) $(WOLFSSL_LIBS) -lpthread

# --- mbedTLS ---
$(BUILD_DIR)/check_pssl_mbedtls: check_pssl_mbedtls.c ../pssl-mbedtls.c ../pssl.c pssl_test_stubs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DP_SSL_MBEDTLS $(MBEDTLS_CFLAGS) $^ -o $@ $(LDFLAGS) $(MBEDTLS_LIBS) -lpthread



# ══════════════════════════════════════════════════════════════════════════════
#  Per-provider network integration tests
# ══════════════════════════════════════════════════════════════════════════════

# --- OpenSSL ---
$(BUILD_DIR)/check_network_openssl: check_network_openssl.c ../pssl-openssl.c ../pssl.c ../papi.c network_test_helpers.c pssl_test_stubs.c | $(BUILD_DIR)
	$(CC) $(NET_CFLAGS) -DP_SSL_OPENSSL $(OPENSSL_CFLAGS) $(SQLITE_CFLAGS) $^ -o $@ $(LDFLAGS) $(OPENSSL_LIBS) -lpthread

# --- OpenSSL 3 ---
$(BUILD_DIR)/check_network_openssl3: check_network_openssl3.c ../pssl-openssl3.c ../pssl.c ../papi.c network_test_helpers.c pssl_test_stubs.c | $(BUILD_DIR)
	$(CC) $(NET_CFLAGS) -DP_SSL_OPENSSL3 $(OPENSSL_CFLAGS) $(SQLITE_CFLAGS) $^ -o $@ $(LDFLAGS) $(OPENSSL_LIBS) -lpthread

# --- WolfSSL ---
$(BUILD_DIR)/check_network_wolfssl: check_network_wolfssl.c ../pssl-wolfssl.c ../pssl.c ../papi.c network_test_helpers.c pssl_test_stubs.c | $(BUILD_DIR)
	$(CC) $(NET_CFLAGS) -DP_SSL_WOLFSSL $(WOLFSSL_CFLAGS) $(SQLITE_CFLAGS) $^ -o $@ $(LDFLAGS) $(WOLFSSL_LIBS) -lpthread

# --- mbedTLS ---
$(BUILD_DIR)/check_network_mbedtls: check_network_mbedtls.c ../pssl-mbedtls.c ../pssl.c ../papi.c network_test_helpers.c pssl_test_stubs.c | $(BUILD_DIR)
	$(CC) $(NET_CFLAGS) -DP_SSL_MBEDTLS $(MBEDTLS_CFLAGS) $(SQLITE_CFLAGS) $^ -o $@ $(LDFLAGS) $(MBEDTLS_LIBS) -lpthread

# ══════════════════════════════════════════════════════════════════════════════
#  Targets
# ══════════════════════════════════════════════════════════════════════════════

all: $(CORE_BINS)

check: $(CORE_BINS)
	@for t in $(CORE_BINS); do echo "=== Running $$t ==="; $$t || exit 1; done

# --- Per-provider SSL convenience targets ---
.PHONY: check-pssl-openssl check-pssl-openssl3 check-pssl-wolfssl check-pssl-mbedtls check-pssl
all: $(TEST_BINS)

check-pssl-openssl: $(BUILD_DIR)/check_pssl_openssl
	$(BUILD_DIR)/check_pssl_openssl

check-pssl-openssl3: $(BUILD_DIR)/check_pssl_openssl3
	$(BUILD_DIR)/check_pssl_openssl3

check-pssl-wolfssl: $(BUILD_DIR)/check_pssl_wolfssl
	$(BUILD_DIR)/check_pssl_wolfssl

check-pssl-mbedtls: $(BUILD_DIR)/check_pssl_mbedtls
	$(BUILD_DIR)/check_pssl_mbedtls

check-pssl: check-pssl-openssl check-pssl-openssl3 check-pssl-wolfssl check-pssl-mbedtls


# --- Per-provider network convenience targets ---
.PHONY: check-network-openssl check-network-openssl3 check-network-wolfssl check-network-mbedtls check-network

check-network-openssl: $(BUILD_DIR)/check_network_openssl
	$(BUILD_DIR)/check_network_openssl

check-network-openssl3: $(BUILD_DIR)/check_network_openssl3
	$(BUILD_DIR)/check_network_openssl3

check-network-wolfssl: $(BUILD_DIR)/check_network_wolfssl
	$(BUILD_DIR)/check_network_wolfssl

check-network-mbedtls: $(BUILD_DIR)/check_network_mbedtls
	$(BUILD_DIR)/check_network_mbedtls

check-network: check-network-openssl check-network-openssl3 check-network-wolfssl check-network-mbedtls

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check clean
