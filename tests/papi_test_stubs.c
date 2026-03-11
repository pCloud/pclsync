/* Stub implementations for papi.c link dependencies during unit testing.
 *
 * Provides mock socket I/O (reads from a global buffer, discards writes)
 * and stubs for timer/settings functions.
 *
 * Compiled without a provider define — uses only standard C types. */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

/* ── Mock socket read buffer ──────────────────────────────────────────────── */
unsigned char *mock_read_buf = NULL;
int mock_read_len = 0;
int mock_read_pos = 0;

/* ── Thread-local name (pcompat.h extern) ─────────────────────────────────── */
__thread const char *psync_thread_name = "test";

/* ── Timer (ptimer.h extern) ──────────────────────────────────────────────── */
time_t psync_current_time;

/* ── Socket stubs (pcompat.h) ─────────────────────────────────────────────── */
typedef struct {
  void *ssl;
  void *buffer;
  int sock;
  int pending;
  uint32_t misc;
} stub_socket;

void *psync_socket_connect(const char *host, unsigned int port, int ssl) {
  (void)host; (void)port; (void)ssl;
  return NULL;
}

void psync_socket_close(void *sock)     { (void)sock; }
void psync_socket_close_bad(void *sock) { (void)sock; }

int psync_socket_readall(void *sock, void *buff, int num) {
  (void)sock;
  if (mock_read_pos + num > mock_read_len)
    return -1;
  memcpy(buff, mock_read_buf + mock_read_pos, num);
  mock_read_pos += num;
  return num;
}

int psync_socket_writeall(void *sock, const void *buff, int num) {
  (void)sock; (void)buff;
  return num;
}

int psync_socket_readall_thread(void *sock, void *buff, int num) {
  return psync_socket_readall(sock, buff, num);
}

int psync_socket_writeall_thread(void *sock, const void *buff, int num) {
  return psync_socket_writeall(sock, buff, num);
}

int psync_socket_readall_v2(void *sock, void *buff, int num, int timeout) {
  (void)timeout;
  return psync_socket_readall(sock, buff, num);
}

int psync_socket_read_noblock(void *sock, void *buff, int num) {
  return psync_socket_readall(sock, buff, num);
}

int psync_socket_isssl(void *sock) {
  (void)sock;
  return 0;
}

/* ── Timer (ptimer.h) ─────────────────────────────────────────────────────── */
time_t psync_timer_time(void) {
  return time(NULL);
}

/* ── Settings (psettings.h) ───────────────────────────────────────────────── */
const char *psync_setting_get_string(int settingid) {
  (void)settingid;
  return NULL;
}

/* ── Debug (plibs.h) — needed at -O0 even with DEBUG_LEVEL=0 ─────────────── */
int psync_debug(const char *file, const char *function, unsigned int line,
                unsigned int level, const char *fmt, ...) {
  (void)file; (void)function; (void)line; (void)level; (void)fmt;
  return 0;
}
