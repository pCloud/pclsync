CC = gcc
BUILD_DIR = build

# --- Check framework detection ---
CHECK_INSTALLED := $(shell pkg-config --exists check 2>/dev/null && echo yes || echo no)

ifeq ($(CHECK_INSTALLED),no)
$(error Check testing framework not found. Install it:\
	Fedora/RHEL: sudo dnf install check check-devel\
	Debian/Ubuntu: sudo apt-get install check\
	macOS: brew install check\
	Docs: https://libcheck.github.io/check/web/install.html)
endif

CHECK_CFLAGS = $(shell pkg-config --cflags check)
CHECK_LIBS   = $(shell pkg-config --libs check)

CFLAGS = -I. -I.. -std=gnu99 -g -Wall $(CHECK_CFLAGS)
LDFLAGS = $(CHECK_LIBS)

TEST_SRCS = $(wildcard check_*.c)
TEST_BINS = $(patsubst %.c,$(BUILD_DIR)/%,$(TEST_SRCS))

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/check_plist: check_plist.c ../plist.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

all: $(TEST_BINS)

check: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "=== Running $$t ==="; $$t || exit 1; done

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check clean
