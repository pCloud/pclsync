CC=cc
BUILD_DIR ?= build

UNAME_S := $(shell uname -s)
UNAME_V := $(shell uname -v)
UNAME_P := $(shell uname -p)
ARCH ?= $(UNAME_P)

CFLAGS=-Wall -Wpointer-arith -O2 -g -fno-stack-protector
ifneq ($(filter x86_64 i686 i386,$(ARCH)),)
  CFLAGS += -fomit-frame-pointer -mtune=core2
endif

ifeq ($(OS),Windows_NT)
    CFLAGS += -DP_OS_WINDOWS
else
    ifeq ($(UNAME_S),Linux)
        CFLAGS += -DP_OS_LINUX
        ifneq (,$(findstring Debian,$(UNAME_V)))
            CFLAGS += -DP_OS_DEBIAN
        endif
	LDFLAGS += -lssl -lcrypto -lpthread
    endif
    ifeq ($(UNAME_S),Darwin)
        CFLAGS += -DP_OS_MACOSX
	LDFLAGS += -lssl -lcrypto -lpthread
    endif
endif

SRCS = overlay_client.c debug.c
OBJ = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

all: overlay_client

fs: overlay_client

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

overlay_client: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) -o $(BUILD_DIR)/overlay_client

clean:
	rm -rf $(BUILD_DIR)
