/* network_test_common.h — shared test cases for per-provider network tests.
 *
 * This header is #included by each provider driver after the provider macro
 * (P_SSL_OPENSSL, P_SSL_OPENSSL3, P_SSL_WOLFSSL, P_SSL_MBEDTLS) is defined
 * so that "pssl.h" resolves to the correct backend.
 *
 * Tests:
 *   - Certificate data validation (fingerprints, PEM certs)
 *   - SSL connection with cert pinning (positive + negative)
 *   - Real API calls (currentserver, getip, getapiserver)
 */

#ifndef NETWORK_TEST_COMMON_H
#define NETWORK_TEST_COMMON_H

#include <check.h>
#include "pssl.h"
#include "papi.h"
#include "psettings.h"
#include "psslcerts.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))
#endif

/* ── Helper: make a binary protocol API call to pCloud ────────────────────── */

static binresult *test_api_call(const char *cmd) {
  psync_socket *sock;
  unsigned char *data;
  size_t cmdlen, plen;
  binresult *res;

  sock = psync_socket_connect(PSYNC_API_HOST, PSYNC_API_PORT_SSL, 1);
  if (!sock) return NULL;

  cmdlen = strlen(cmd);
  data = do_prepare_command(cmd, cmdlen, NULL, 0, -1, 0, &plen);
  if (!data) { psync_socket_close(sock); return NULL; }

  if (psync_socket_writeall(sock, data, plen) != (int)plen) {
    free(data);
    psync_socket_close(sock);
    return NULL;
  }
  free(data);

  res = get_result(sock);
  psync_socket_close(sock);
  return res;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Certificate data validation
 * ═══════════════════════════════════════════════════════════════════════════ */

START_TEST(cert_pk_sha256_count) {
  ck_assert_uint_eq(ARRAY_SIZE(psync_ssl_trusted_pk_sha256), (size_t)18);
}
END_TEST

START_TEST(cert_pk_sha256_format) {
  size_t i, j;
  for (i = 0; i < ARRAY_SIZE(psync_ssl_trusted_pk_sha256); i++) {
    size_t len = strlen(psync_ssl_trusted_pk_sha256[i]);
    ck_assert_uint_eq(len, (size_t)64);
    for (j = 0; j < len; j++)
      ck_assert(isxdigit((unsigned char)psync_ssl_trusted_pk_sha256[i][j]));
  }
}
END_TEST

START_TEST(cert_pem_count) {
  ck_assert_uint_eq(ARRAY_SIZE(psync_ssl_trusted_certs), (size_t)31);
}
END_TEST

START_TEST(cert_pem_begin_end) {
  size_t i;
  for (i = 0; i < ARRAY_SIZE(psync_ssl_trusted_certs); i++) {
    ck_assert(strstr(psync_ssl_trusted_certs[i], "-----BEGIN CERTIFICATE-----") != NULL);
    ck_assert(strstr(psync_ssl_trusted_certs[i], "-----END CERTIFICATE-----") != NULL);
  }
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════════════
 *  SSL connection tests
 * ═══════════════════════════════════════════════════════════════════════════ */

START_TEST(ssl_connect_pcloud) {
  psync_socket *sock = psync_socket_connect(PSYNC_API_HOST, PSYNC_API_PORT_SSL, 1);
  ck_assert_ptr_nonnull(sock);
  psync_socket_close(sock);
}
END_TEST

START_TEST(ssl_connect_untrusted_fails) {
  /* google.com's certificate is not in pCloud's pinned trust store,
   * so the SSL handshake should fail. */
  psync_socket *sock = psync_socket_connect("www.google.com", 443, 1);
  ck_assert_ptr_null(sock);
}
END_TEST

/* ═══════════════════════════════════════════════════════════════════════════
 *  API call tests
 * ═══════════════════════════════════════════════════════════════════════════ */

START_TEST(api_currentserver) {
  binresult *res = test_api_call("currentserver");
  ck_assert_ptr_nonnull(res);

  const binresult *result_num = psync_find_result(res, "result", PARAM_NUM);
  ck_assert_uint_eq(result_num->num, (uint64_t)0);

  const binresult *hostname = psync_check_result(res, "hostname", PARAM_STR);
  ck_assert_ptr_nonnull(hostname);
  ck_assert(hostname->length > 0);

  const binresult *ip = psync_check_result(res, "ip", PARAM_STR);
  ck_assert_ptr_nonnull(ip);
  ck_assert(ip->length > 0);

  free(res);
}
END_TEST

START_TEST(api_getip) {
  binresult *res = test_api_call("getip");
  ck_assert_ptr_nonnull(res);

  const binresult *result_num = psync_find_result(res, "result", PARAM_NUM);
  ck_assert_uint_eq(result_num->num, (uint64_t)0);

  const binresult *ip = psync_check_result(res, "ip", PARAM_STR);
  ck_assert_ptr_nonnull(ip);
  ck_assert(ip->length > 0);

  free(res);
}
END_TEST

START_TEST(api_getapiserver) {
  binresult *res = test_api_call("getapiserver");
  ck_assert_ptr_nonnull(res);

  const binresult *result_num = psync_find_result(res, "result", PARAM_NUM);
  ck_assert_uint_eq(result_num->num, (uint64_t)0);

  const binresult *binapi = psync_check_result(res, "binapi", PARAM_ARRAY);
  ck_assert_ptr_nonnull(binapi);
  ck_assert(binapi->length >= 1);

  const binresult *api = psync_check_result(res, "api", PARAM_ARRAY);
  ck_assert_ptr_nonnull(api);
  ck_assert(api->length >= 1);

  free(res);
}
END_TEST

#endif /* NETWORK_TEST_COMMON_H */
