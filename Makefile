CC=gcc
AR=ar rcu
RANLIB=ranlib
BUILD_DIR ?= build
USESSL?=

LIB_A=psynclib.a

GCC_OPTIMIZATION_LEVEL ?= s

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
        LDFLAGS += -lfuse -lpthread -lsqlite3 -lzlib
    endif

    ifeq ($(UNAME_S),Darwin)
		CFLAGS=-DP_OS_MACOSX -Wall -Wpointer-arith -O$(GCC_OPTIMIZATION_LEVEL) -g -pg
		ifneq ($(filter x86_64 i686 i386,$(ARCH)),)
			CFLAGS += -mtune=core2
		endif

        CFLAGS+=-Wno-error=int-conversion -Wno-error=incompatible-function-pointer-types
		LDFLAGS += -losxfuse -lsqlite3 -framework Cocoa -L/usr/local/ssl/lib
    endif
endif

ifdef SQLITE_INCLUDE_DIR
	CFLAGS+=-I$(SQLITE_INCLUDE_DIR)
endif
ifdef FUSE_INCLUDE_DIR
	CFLAGS+=-I$(FUSE_INCLUDE_DIR)
endif

ifeq ($(USESSL),openssl)
  SRCS += pssl-openssl.c
  CFLAGS += -DP_SSL_OPENSSL
endif
ifeq ($(USESSL),securetransport)
  SRCS += pssl-securetransport.o
  CFLAGS += -DP_SSL_SECURETRANSPORT
endif
ifeq ($(USESSL),mbed)
  SRCS += pssl-mbedtls.c
  CFLAGS += -DP_SSL_MBEDTLS -I../mbedtls/include/
endif
ifeq ($(USESSL),wolfssl)
  WOLFSSL_CFLAGS = -DP_SSL_WOLFSSL

  ifdef WOLFSSL_INCLUDE_DIR
    WOLFSSL_CFLAGS += -I$(WOLFSSL_INCLUDE_DIR)
  else
    WOLFSSL_CFLAGS += -I../wolfssl/ -I../wolfssl/wolfssl/
  endif

  SRCS += pssl-wolfssl.c
  CFLAGS += $(WOLFSSL_CFLAGS)
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

clean:
	rm -rf $(BUILD_DIR) ./lib/poverlay_linux/*.o ./lib/poverlay_linux/overlay_client

