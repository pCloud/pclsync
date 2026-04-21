/* Link-time stubs for papi.c dependencies in test builds */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

typedef int psync_socket_t;
typedef struct {
  psync_socket_t sock;
  int pending;
  void *ssl;
} psync_socket;

psync_socket *psync_socket_connect(const char *host, unsigned int port, int ssl) {
  (void)host; (void)port; (void)ssl;
  return NULL;
}

void psync_socket_close(psync_socket *sock) { (void)sock; }
void psync_socket_close_bad(psync_socket *sock) { (void)sock; }

int psync_socket_readall(psync_socket *sock, void *buf, int num) {
  (void)sock; (void)buf; (void)num;
  return -1;
}

int psync_socket_readall_v2(psync_socket *sock, void *buf, int num, int timeout) {
  (void)sock; (void)buf; (void)num; (void)timeout;
  return -1;
}

int psync_socket_readall_thread(psync_socket *sock, void *buf, int num) {
  (void)sock; (void)buf; (void)num;
  return -1;
}

int psync_socket_read_noblock(psync_socket *sock, void *buf, int num) {
  (void)sock; (void)buf; (void)num;
  return -1;
}

int psync_socket_writeall(psync_socket *sock, const void *buf, int num) {
  (void)sock; (void)buf; (void)num;
  return -1;
}

int psync_socket_writeall_thread(psync_socket *sock, const void *buf, int num) {
  (void)sock; (void)buf; (void)num;
  return -1;
}

int psync_socket_isssl(psync_socket *sock) {
  (void)sock;
  return 0;
}

void psync_apipool_set_server(const char *server) { (void)server; }

const char *psync_setting_get_string(const char *key) {
  (void)key;
  return NULL;
}

time_t psync_timer_time(void) {
  return time(NULL);
}

int psync_debug(const char *file, const char *function, unsigned int line,
                unsigned int level, const char *fmt, ...) {
  (void)file; (void)function; (void)line; (void)level; (void)fmt;
  return 1;
}

void *psync_malloc(size_t size) { return malloc(size); }
void *psync_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
void psync_free(void *ptr) { free(ptr); }
