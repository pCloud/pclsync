CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
ARFLAGS ?= rcu
RANLIB = $(CROSS_COMPILE)ranlib
BUILD_DIR ?= build
USESSL?=

LIB_A=psynclib.a

GCC_OPTIMIZATION_LEVEL ?= s

# ── Shared library versioning ────────────────────────────────────────────────
LIB_SO_BASE = libpsynclib
LIB_SO_VERSION = 2.25.11

# ── Dependency discovery (pkg-config with manual overrides) ────────────────
ifdef CROSS_COMPILE
  _CROSS_TRIPLE := $(patsubst %-,%,$(CROSS_COMPILE))
  PKG_CONFIG ?= $(_CROSS_TRIPLE)-pkg-config
else
  PKG_CONFIG ?= pkg-config
endif
HAS_PKG_CONFIG := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && echo yes)
ifeq ($(HAS_PKG_CONFIG),yes)
  pkg_cflags = $(shell $(PKG_CONFIG) --cflags $(1) 2>/dev/null)
  pkg_libs   = $(shell $(PKG_CONFIG) --libs $(1) 2>/dev/null)
else
  pkg_cflags =
  pkg_libs   =
endif

# SQLite3
ifdef SQLITE_INCLUDE_DIR
  ifneq ($(SQLITE_INCLUDE_DIR),)
    SQLITE_CFLAGS = -I$(SQLITE_INCLUDE_DIR)
  endif
else
  SQLITE_CFLAGS := $(call pkg_cflags,sqlite3)
endif
SQLITE_LIBS := $(call pkg_libs,sqlite3)
ifeq ($(SQLITE_LIBS),)
  SQLITE_LIBS = -lsqlite3
endif

# FUSE (resolved per-platform below)
ifdef FUSE_INCLUDE_DIR
  ifneq ($(FUSE_INCLUDE_DIR),)
    FUSE_CFLAGS = -I$(FUSE_INCLUDE_DIR)
  endif
else
  FUSE_CFLAGS := $(call pkg_cflags,fuse)
endif
FUSE_LIBS := $(call pkg_libs,fuse)

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

# ── SSL provider auto-detection ──────────────────────────────────────────────
ifndef USESSL
  ifeq ($(HAS_PKG_CONFIG),yes)
    # Try OpenSSL first — check major version to pick openssl3 vs openssl
    _OPENSSL_VERSION := $(shell $(PKG_CONFIG) --modversion openssl 2>/dev/null)
    ifneq ($(_OPENSSL_VERSION),)
      _OPENSSL_MAJOR := $(word 1,$(subst ., ,$(_OPENSSL_VERSION)))
      ifeq ($(shell [ $(_OPENSSL_MAJOR) -ge 3 ] 2>/dev/null && echo yes),yes)
        USESSL = openssl3
      else
        USESSL = openssl
      endif
    endif
  endif
  # WolfSSL
  ifndef USESSL
    ifeq ($(HAS_PKG_CONFIG),yes)
      ifneq ($(shell $(PKG_CONFIG) --exists wolfssl 2>/dev/null && echo yes),)
        USESSL = wolfssl
      endif
    endif
  endif
  # mbedTLS
  ifndef USESSL
    ifeq ($(HAS_PKG_CONFIG),yes)
      ifneq ($(shell $(PKG_CONFIG) --exists mbedtls 2>/dev/null && echo yes),)
        USESSL = mbed
      endif
    endif
  endif
  # On macOS, fall back to SecureTransport (always available)
  ifndef USESSL
    ifeq ($(shell uname -s 2>/dev/null),Darwin)
      USESSL = securetransport
    endif
  endif

  # Report what we found (or didn't)
  ifdef USESSL
    _USESSL_AUTODETECTED = 1
    $(info [SSL] Auto-detected provider: $(USESSL))
  endif
endif

SRCS := pcompat.c psynclib.c plocks.c plibs.c pcallbacks.c pdiff.c pstatus.c papi.c ptimer.c pupload.c pdownload.c pfolder.c\
            psyncer.c ptasks.c psettings.c pnetlibs.c pcache.c pscanner.c plist.c plocalscan.c plocalnotify.c pp2p.c\
            pcrypto.c pssl.c pfileops.c ptree.c ppassword.c prunratelimit.c pmemlock.c pnotifications.c pexternalstatus.c publiclinks.c\
            pbusinessaccount.c pcontacts.c poverlay.c pcompression.c pasyncnet.c ppathstatus.c\
            pdevice_monitor.c ptools.c pstrings.c pencoding.c pqsort.c miniz.c
SRCSFS := pfs.c ppagecache.c pfsfolder.c pfstasks.c pfsupload.c pintervaltree.c pfsxattr.c pcloudcrypto.c pfscrypto.c pcrc32c.c pfsstatic.c

ifeq ($(OS),Windows_NT)
    CFLAGS=-DP_OS_WINDOWS
    LIB_A=psynclib.dll
    AR=$(CC) -shared -o
RANLIB=strip --strip-unneeded
    LDFLAGS=-s
else
    UNAME_S	:= $(shell uname -s)
    UNAME_V	:= $(shell uname -v)
    UNAME_P	:= $(shell uname -p)
    ifdef CROSS_COMPILE
      ARCH ?= $(firstword $(subst -, ,$(_CROSS_TRIPLE)))
    else
      ARCH ?= $(UNAME_P)
    endif

    ifeq ($(UNAME_S),Linux)
		CFLAGS=-DP_OS_LINUX -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -Wall -Wpointer-arith -O2 -g -fno-stack-protector -fPIC -std=gnu99
        ifneq ($(filter x86_64 i686 i386,$(ARCH)),)
        	CFLAGS += -fomit-frame-pointer -mtune=core2
        endif
        ifneq (,$(findstring Debian,$(UNAME_V)))
        	CFLAGS += -DP_OS_DEBIAN
        endif
        CFLAGS += $(SQLITE_CFLAGS) $(FUSE_CFLAGS)
        ifeq ($(FUSE_LIBS),)
          FUSE_LIBS = -lfuse
        endif
        LDFLAGS += $(FUSE_LIBS) -lpthread $(SQLITE_LIBS) -lz -ludev
    endif

    ifeq ($(UNAME_S),Darwin)
		CFLAGS=-DP_OS_MACOSX -Wall -Wpointer-arith -O$(GCC_OPTIMIZATION_LEVEL) -g -pg
		ifneq ($(filter x86_64 i686 i386,$(ARCH)),)
			CFLAGS += -mtune=core2
		endif

        CFLAGS += $(SQLITE_CFLAGS) $(FUSE_CFLAGS)
        ifeq ($(FUSE_LIBS),)
          FUSE_LIBS = -lfuse
        endif
		LDFLAGS += $(FUSE_LIBS) $(SQLITE_LIBS) -lz -framework Cocoa -framework IOKit
    endif
endif

ifeq ($(USESSL),openssl)
  SRCS += pssl-openssl.c
  CFLAGS += -DP_SSL_OPENSSL $(OPENSSL_CFLAGS)
  LDFLAGS += $(OPENSSL_LIBS)
endif
ifeq ($(USESSL),openssl3)
  SRCS += pssl-openssl3.c
  CFLAGS += -DP_SSL_OPENSSL3 $(OPENSSL_CFLAGS)
  LDFLAGS += $(OPENSSL_LIBS)
endif
ifeq ($(USESSL),securetransport)
  SRCS += pssl-securetransport.c
  CFLAGS += -DP_SSL_SECURETRANSPORT
endif
ifeq ($(USESSL),mbed)
  SRCS += pssl-mbedtls.c
  CFLAGS += -DP_SSL_MBEDTLS $(MBEDTLS_CFLAGS)
  LDFLAGS += $(MBEDTLS_LIBS)
endif
ifeq ($(USESSL),wolfssl)
  SRCS += pssl-wolfssl.c
  CFLAGS += -DP_SSL_WOLFSSL $(WOLFSSL_CFLAGS)
  LDFLAGS += $(WOLFSSL_LIBS)
endif

# Validate USESSL value
ifneq ($(USESSL),)
  ifeq ($(filter openssl openssl3 securetransport mbed wolfssl,$(USESSL)),)
    $(error Unknown USESSL=$(USESSL). Valid: openssl openssl3 mbed wolfssl securetransport)
  endif
  ifndef _USESSL_AUTODETECTED
    $(info [SSL] Using provider: $(USESSL))
  endif
endif

ifdef DEBUG_LEVEL
    CFLAGS += -DDEBUG_LEVEL=$(DEBUG_LEVEL)
endif

CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

OBJ=$(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))
OBJFS=$(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCSFS))
OBJNOFS=$(BUILD_DIR)/pfsfake.o

OBJ1=overlay_client.o

all: $(BUILD_DIR)/$(LIB_A)

$(BUILD_DIR) $(BUILD_DIR)/include $(BUILD_DIR)/lib:
	mkdir -p $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(LIB_A): $(OBJ) $(OBJNOFS) | $(BUILD_DIR)
	$(AR) $(ARFLAGS) $@ $(OBJ) $(OBJNOFS)
	$(RANLIB) $@

fs: $(OBJ) $(OBJFS) | $(BUILD_DIR)
	$(AR) $(ARFLAGS) $(BUILD_DIR)/$(LIB_A) $(OBJ) $(OBJFS)
	$(RANLIB) $(BUILD_DIR)/$(LIB_A)

cli: fs
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/cli cli.c cli_ipc.c $(BUILD_DIR)/$(LIB_A) $(LDFLAGS)

overlay_client:
	cd ./lib/poverlay_linux && make

install-headers: | $(BUILD_DIR)/include
	cp *.h $(BUILD_DIR)/include/

install: $(BUILD_DIR)/$(LIB_A) install-headers | $(BUILD_DIR)/lib
	cp $(BUILD_DIR)/$(LIB_A) $(BUILD_DIR)/lib/

install-fs: fs install-headers | $(BUILD_DIR)/lib
	cp $(BUILD_DIR)/$(LIB_A) $(BUILD_DIR)/lib/

# === Shared library targets ===
ifeq ($(UNAME_S),Darwin)
  LIB_SO = $(LIB_SO_BASE).dylib
  LIB_SO_VERSIONED = $(LIB_SO_BASE).$(LIB_SO_VERSION).dylib
  SHARED_LDFLAGS = -dynamiclib -Wl,-install_name,$(LIB_SO_BASE).$(word 1,$(subst ., ,$(LIB_SO_VERSION))).dylib
else
  LIB_SO = $(LIB_SO_BASE).so
  LIB_SO_VERSIONED = $(LIB_SO_BASE).so.$(LIB_SO_VERSION)
  LIB_SO_MAJOR = $(LIB_SO_BASE).so.$(word 1,$(subst ., ,$(LIB_SO_VERSION)))
  SHARED_LDFLAGS = -shared -Wl,-soname,$(LIB_SO_MAJOR)
endif

shared: $(OBJ) $(OBJNOFS)
	$(CC) $(SHARED_LDFLAGS) -o $(BUILD_DIR)/$(LIB_SO_VERSIONED) $(OBJ) $(OBJNOFS) $(LDFLAGS)
ifeq ($(UNAME_S),Darwin)
	ln -sf $(LIB_SO_VERSIONED) $(BUILD_DIR)/$(LIB_SO)
else
	ln -sf $(LIB_SO_VERSIONED) $(BUILD_DIR)/$(LIB_SO_MAJOR)
	ln -sf $(LIB_SO_MAJOR) $(BUILD_DIR)/$(LIB_SO)
endif

shared-fs: $(OBJ) $(OBJFS)
	$(CC) $(SHARED_LDFLAGS) -o $(BUILD_DIR)/$(LIB_SO_VERSIONED) $(OBJ) $(OBJFS) $(LDFLAGS)
ifeq ($(UNAME_S),Darwin)
	ln -sf $(LIB_SO_VERSIONED) $(BUILD_DIR)/$(LIB_SO)
else
	ln -sf $(LIB_SO_VERSIONED) $(BUILD_DIR)/$(LIB_SO_MAJOR)
	ln -sf $(LIB_SO_MAJOR) $(BUILD_DIR)/$(LIB_SO)
endif

check:
	$(MAKE) -C tests check

test: check

# ── Integration test variables ───────────────────────────────────────────────
CLI_DATA_DIR          ?= /tmp/pcloud-ci-data
DRIVE_PATH            ?= /tmp/pcloud-ci-mount
MOUNT_TIMEOUT         ?= 30
CLI_WRAPPER           ?=
PYTEST_MARKER         ?=
SKIP_VENV             ?=
CLEAN_TEST_FOLDER     ?=
TEST_ROOT             ?= /integration-tests/
PCLOUD_CRYPTO_SECRET  ?=
CRYPTO_TEST_ROOT      ?= /Crypto Folder/integration-tests/
CRYPTO_UNLOCK_TIMEOUT ?= 30

integration-test: cli
	@for var in PCLOUD_AUTH_TOKEN PCLOUD_API_SERVER PCLOUD_LOCATION_ID; do \
	  eval val=\$$$$var; \
	  if [ -z "$$val" ]; then \
	    echo "ERROR: $$var is not set"; \
	    exit 1; \
	  fi; \
	done; \
	mkdir -p $(CLI_DATA_DIR) $(DRIVE_PATH); \
	echo "Starting CLI: mount=$(DRIVE_PATH) data=$(CLI_DATA_DIR)"; \
	$(CLI_WRAPPER) $(BUILD_DIR)/cli start \
	  --auth $(PCLOUD_AUTH_TOKEN) \
	  --mountpoint $(DRIVE_PATH) \
	  --datadir $(CLI_DATA_DIR) \
	  --apiserver $(PCLOUD_API_SERVER) \
	  --locationid $(PCLOUD_LOCATION_ID) & \
	CLI_PID=$$!; \
	echo "CLI PID: $$CLI_PID"; \
	ELAPSED=0; \
	while [ $$ELAPSED -lt $(MOUNT_TIMEOUT) ]; do \
	  if mountpoint -q $(DRIVE_PATH) 2>/dev/null || mount | grep -q " $(DRIVE_PATH) "; then \
	    break; \
	  fi; \
	  sleep 1; \
	  ELAPSED=$$((ELAPSED + 1)); \
	done; \
	if ! mountpoint -q $(DRIVE_PATH) 2>/dev/null && ! mount | grep -q " $(DRIVE_PATH) "; then \
	  echo "ERROR: FUSE mount did not appear within $(MOUNT_TIMEOUT)s"; \
	  kill $$CLI_PID 2>/dev/null || true; \
	  exit 1; \
	fi; \
	echo "FUSE mount ready after $${ELAPSED}s"; \
	EFFECTIVE_TEST_ROOT="$(TEST_ROOT)"; \
	if [ -n "$(PCLOUD_CRYPTO_SECRET)" ]; then \
	  echo "Unlocking crypto folder..."; \
	  PCLOUD_CRYPTO_SECRET="$(PCLOUD_CRYPTO_SECRET)" \
	    $(BUILD_DIR)/cli crypto unlock --datadir $(CLI_DATA_DIR) \
	    || { echo "ERROR: Failed to send crypto unlock"; kill $$CLI_PID 2>/dev/null || true; exit 1; }; \
	  ELAPSED=0; STATE=; \
	  while [ $$ELAPSED -lt $(CRYPTO_UNLOCK_TIMEOUT) ]; do \
	    STATE=$$($(BUILD_DIR)/cli crypto status --just-unlock-state --datadir $(CLI_DATA_DIR) 2>/dev/null); \
	    if [ "$$STATE" = "unlocked" ]; then break; fi; \
	    if [ "$$STATE" = "not setup" ]; then \
	      echo "ERROR: Crypto is not set up on this account"; \
	      kill $$CLI_PID 2>/dev/null || true; exit 1; \
	    fi; \
	    sleep 1; ELAPSED=$$((ELAPSED + 1)); \
	  done; \
	  if [ "$$STATE" != "unlocked" ]; then \
	    echo "ERROR: Crypto did not unlock within $(CRYPTO_UNLOCK_TIMEOUT)s (state: $$STATE)"; \
	    kill $$CLI_PID 2>/dev/null || true; exit 1; \
	  fi; \
	  echo "Crypto unlocked after $${ELAPSED}s"; \
	  EFFECTIVE_TEST_ROOT="$(CRYPTO_TEST_ROOT)"; \
	fi; \
	if [ -n "$(SYNC_FOLDER_LOCAL_PATH)" ] && [ -n "$(SYNC_FOLDER_REMOTE_PATH)" ]; then \
	  mkdir -p "$(SYNC_FOLDER_LOCAL_PATH)"; \
	  echo "Creating sync: $(SYNC_FOLDER_LOCAL_PATH) <-> $(SYNC_FOLDER_REMOTE_PATH)"; \
	  $(BUILD_DIR)/cli sync add \
	    --datadir $(CLI_DATA_DIR) \
	    --local "$(SYNC_FOLDER_LOCAL_PATH)" \
	    --remote "$(SYNC_FOLDER_REMOTE_PATH)" \
	    --type full || { echo "ERROR: Failed to create sync"; kill $$CLI_PID 2>/dev/null || true; exit 1; }; \
	fi; \
	if [ -z "$(SKIP_VENV)" ]; then \
	  python3 -m venv integration-tests/venv; \
	  . integration-tests/venv/bin/activate; \
	  pip install --quiet -r integration-tests/requirements.txt; \
	fi; \
	PYTEST_EXIT=0; \
	DRIVE_PATH=$(DRIVE_PATH) \
	PCLOUD_AUTH_TOKEN=$(PCLOUD_AUTH_TOKEN) \
	PCLOUD_API_SERVER=$(PCLOUD_API_SERVER) \
	PCLOUD_LOCATION_ID=$(PCLOUD_LOCATION_ID) \
	SYNC_FOLDER_LOCAL_PATH="$(SYNC_FOLDER_LOCAL_PATH)" \
	SYNC_FOLDER_REMOTE_PATH="$(SYNC_FOLDER_REMOTE_PATH)" \
	TEST_ROOT="$$EFFECTIVE_TEST_ROOT" \
	CLEAN_TEST_FOLDER="$(CLEAN_TEST_FOLDER)" \
	  python3 -m pytest integration-tests/ \
	  $(if $(PYTEST_MARKER),-m "$(PYTEST_MARKER)",) \
	  || PYTEST_EXIT=$$?; \
	$(BUILD_DIR)/cli stop --datadir $(CLI_DATA_DIR) 2>/dev/null || kill $$CLI_PID 2>/dev/null || true; \
	WAIT_STOP=0; while kill -0 $$CLI_PID 2>/dev/null && [ $$WAIT_STOP -lt 10 ]; do sleep 1; WAIT_STOP=$$((WAIT_STOP+1)); done; \
	fusermount -u $(DRIVE_PATH) 2>/dev/null || umount $(DRIVE_PATH) 2>/dev/null || true; \
	exit $$PYTEST_EXIT

_DRIVE_MARKER = filesystem$(if $(PYTEST_MARKER), and $(PYTEST_MARKER),)
_SYNC_MARKER  = sync$(if $(PYTEST_MARKER), and $(PYTEST_MARKER),)

integration-test-drive: cli
	$(MAKE) integration-test PYTEST_MARKER="$(_DRIVE_MARKER)" \
	  SYNC_FOLDER_LOCAL_PATH= SYNC_FOLDER_REMOTE_PATH= \
	  PCLOUD_CRYPTO_SECRET=

integration-test-sync: cli
	$(MAKE) integration-test PYTEST_MARKER="$(_SYNC_MARKER)" \
	  SYNC_FOLDER_LOCAL_PATH="$(SYNC_FOLDER_LOCAL_PATH)" \
	  SYNC_FOLDER_REMOTE_PATH="$(SYNC_FOLDER_REMOTE_PATH)" \
	  PCLOUD_CRYPTO_SECRET=

integration-test-drive-crypto: cli
	@if [ -z "$(PCLOUD_CRYPTO_SECRET)" ]; then \
	  echo "ERROR: PCLOUD_CRYPTO_SECRET is not set"; exit 1; \
	fi
	$(MAKE) integration-test PYTEST_MARKER="$(_DRIVE_MARKER)" \
	  PCLOUD_CRYPTO_SECRET="$(PCLOUD_CRYPTO_SECRET)" \
	  SYNC_FOLDER_LOCAL_PATH= SYNC_FOLDER_REMOTE_PATH=

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C ./lib/poverlay_linux clean
	$(MAKE) -C tests clean
	rm -f $(LIB_SO_BASE).so $(LIB_SO_BASE).so.* $(LIB_SO_BASE).dylib $(LIB_SO_BASE).*.dylib

.PHONY: shared shared-fs check test integration-test integration-test-drive integration-test-sync integration-test-drive-crypto clean

