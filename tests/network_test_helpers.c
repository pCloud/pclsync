/* Real socket connect/read/write using the active SSL provider.
 *
 * Compiled per-provider (needs -DP_SSL_<PROVIDER> flag) so pssl.h
 * resolves to the correct backend.
 *
 * Provides the psync_socket_* functions that papi.c expects, backed
 * by real network I/O with SSL support. */

// Use the real headers, not the test-specific shims.
#include "../pcompat.h"
#include "../pssl.h"
#include "../psettings.h"

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* ── Globals required by pcompat.h / ptimer.h ─────────────────────────────── */
PSYNC_THREAD const char *psync_thread_name = "test";
time_t psync_current_time;

static int ssl_initialized = 0;

/* ── Socket operations ────────────────────────────────────────────────────── */

psync_socket *psync_socket_connect(const char *host, unsigned int port, int ssl) {
  struct addrinfo hints, *res, *rp;
  char portstr[16];
  int fd = -1;
  psync_socket *sock;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(portstr, sizeof(portstr), "%u", port);

  if (getaddrinfo(host, portstr, &hints, &res) != 0)
    return NULL;

  for (rp = res; rp; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd == -1) continue;
    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd == -1) return NULL;

  sock = (psync_socket *)malloc(sizeof(psync_socket));
  memset(sock, 0, sizeof(*sock));
  sock->sock = fd;
  sock->ssl = NULL;

  if (ssl) {
    int ret;
    if (!ssl_initialized) {
      if (psync_ssl_init() != 0) { close(fd); free(sock); return NULL; }
      ssl_initialized = 1;
    }
    ret = psync_ssl_connect(fd, &sock->ssl, host);
    if (ret == PSYNC_SSL_NEED_FINISH) {
      while ((ret = psync_ssl_connect_finish(sock->ssl, host)) == PSYNC_SSL_NEED_FINISH)
        usleep(1000);
    }
    if (ret != PSYNC_SSL_SUCCESS) {
      if (sock->ssl) psync_ssl_free(sock->ssl);
      close(fd);
      free(sock);
      return NULL;
    }
  }

  return sock;
}

void psync_socket_close(psync_socket *sock) {
  if (!sock) return;
  if (sock->ssl) {
    /* psync_ssl_shutdown frees the conn on PSYNC_SSL_SUCCESS;
     * only call psync_ssl_free if shutdown needs more I/O. */
    int ret = psync_ssl_shutdown(sock->ssl);
    if (ret == PSYNC_SSL_NEED_FINISH)
      psync_ssl_free(sock->ssl);
  }
  close(sock->sock);
  free(sock);
}

void psync_socket_close_bad(psync_socket *sock) {
  psync_socket_close(sock);
}

int psync_socket_readall(psync_socket *sock, void *buff, int num) {
  int rd, total = 0;
  while (total < num) {
    if (sock->ssl) {
      rd = psync_ssl_read(sock->ssl, (char *)buff + total, num - total);
      if (rd < 0) {
        if (psync_ssl_errno == PSYNC_SSL_ERR_WANT_READ ||
            psync_ssl_errno == PSYNC_SSL_ERR_WANT_WRITE)
          continue;
        return total > 0 ? total : -1;
      }
    } else {
      rd = read(sock->sock, (char *)buff + total, num - total);
    }
    if (rd <= 0) return total > 0 ? total : -1;
    total += rd;
  }
  return total;
}

int psync_socket_writeall(psync_socket *sock, const void *buff, int num) {
  int wr, total = 0;
  while (total < num) {
    if (sock->ssl) {
      wr = psync_ssl_write(sock->ssl, (const char *)buff + total, num - total);
      if (wr < 0) {
        if (psync_ssl_errno == PSYNC_SSL_ERR_WANT_READ ||
            psync_ssl_errno == PSYNC_SSL_ERR_WANT_WRITE)
          continue;
        return total > 0 ? total : -1;
      }
    } else {
      wr = write(sock->sock, (const char *)buff + total, num - total);
    }
    if (wr <= 0) return total > 0 ? total : -1;
    total += wr;
  }
  return total;
}

int psync_socket_readall_thread(psync_socket *sock, void *buff, int num) {
  return psync_socket_readall(sock, buff, num);
}

int psync_socket_writeall_thread(psync_socket *sock, const void *buff, int num) {
  return psync_socket_writeall(sock, buff, num);
}

int psync_socket_readall_v2(psync_socket *sock, void *buff, int num, int timeout) {
  (void)timeout;
  return psync_socket_readall(sock, buff, num);
}

int psync_socket_read_noblock(psync_socket *sock, void *buff, int num) {
  return psync_socket_readall(sock, buff, num);
}

int psync_socket_isssl(psync_socket *sock) {
  return sock->ssl != NULL;
}

/* ── Timer ────────────────────────────────────────────────────────────────── */
time_t psync_timer_time(void) {
  return time(NULL);
}

/* ── Settings ─────────────────────────────────────────────────────────────── */
const char *psync_setting_get_string(psync_settingid_t settingid) {
  (void)settingid;
  return NULL;
}

int psync_setting_get_bool(psync_settingid_t settingid) {
  (void)settingid;
  return 0;
}

/* ── Debug (needed by papi.c at -O0) ──────────────────────────────────────── */
int psync_debug(const char *file, const char *function, unsigned int line,
                unsigned int level, const char *fmt, ...) {
  (void)file; (void)function; (void)line; (void)level; (void)fmt;
  return 0;
}
