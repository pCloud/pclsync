CC=gcc
AR=ar rcu
RANLIB=ranlib
BUILD_DIR ?= build
USESSL?=

LIB_A=psynclib.a

GCC_OPTIMIZATION_LEVEL ?= s

# ── Shared library versioning ────────────────────────────────────────────────
LIB_SO_BASE = libpsynclib
LIB_SO_VERSION = 2.25.11

# ── Dependency discovery (pkg-config with manual overrides) ────────────────
PKG_CONFIG ?= pkg-config
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
            pdevice_monitor.c ptools.c miniz.c
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
    ARCH ?= $(UNAME_P)

    ifeq ($(UNAME_S),Linux)
		CFLAGS=-DP_OS_LINUX -D_FILE_OFFSET_BITS=64 -Wall -Wpointer-arith -O2 -g -fno-stack-protector -fPIC -std=gnu99
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
        LDFLAGS += $(FUSE_LIBS) -lpthread $(SQLITE_LIBS) -lz
    endif

    ifeq ($(UNAME_S),Darwin)
		CFLAGS=-DP_OS_MACOSX -Wall -Wpointer-arith -O$(GCC_OPTIMIZATION_LEVEL) -g -pg
		ifneq ($(filter x86_64 i686 i386,$(ARCH)),)
			CFLAGS += -mtune=core2
		endif

        CFLAGS+=-Wno-error=int-conversion -Wno-error=incompatible-function-pointer-types
        CFLAGS += $(SQLITE_CFLAGS) $(FUSE_CFLAGS)
        ifeq ($(FUSE_LIBS),)
          FUSE_LIBS = -losxfuse
        endif
		LDFLAGS += $(FUSE_LIBS) $(SQLITE_LIBS) -framework Cocoa
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
	$(AR) $@ $(OBJ) $(OBJNOFS)
	$(RANLIB) $@

fs: $(OBJ) $(OBJFS) | $(BUILD_DIR)
	$(AR) $(BUILD_DIR)/$(LIB_A) $(OBJ) $(OBJFS)
	$(RANLIB) $(BUILD_DIR)/$(LIB_A)

cli: fs
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/cli cli.c $(BUILD_DIR)/$(LIB_A) $(LDFLAGS)

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

clean:
	rm -rf $(BUILD_DIR) ./lib/poverlay_linux/*.o ./lib/poverlay_linux/overlay_client
	rm -f $(LIB_SO_BASE).so $(LIB_SO_BASE).so.* $(LIB_SO_BASE).dylib $(LIB_SO_BASE).*.dylib

.PHONY: shared shared-fs check test clean

