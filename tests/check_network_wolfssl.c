/* WolfSSL network integration test driver.
 *
 * Build:  make check-network-wolfssl   (requires WolfSSL)
 * Run:    ./tests/check_network_wolfssl
 */

#include <check.h>
#include <stdlib.h>

#ifndef P_SSL_WOLFSSL
#define P_SSL_WOLFSSL
#endif
#include "pssl.h"
#include <wolfssl/wolfcrypt/random.h>

static void provider_init(void) {
  wolfSSL_Init();
}

#include "network_test_common.h"

static Suite *network_wolfssl_suite(void) {
  Suite *s = suite_create("network_wolfssl");

  TCase *tc_certs = tcase_create("certs");
  tcase_add_test(tc_certs, cert_pk_sha256_count);
  tcase_add_test(tc_certs, cert_pk_sha256_format);
  tcase_add_test(tc_certs, cert_pem_count);
  tcase_add_test(tc_certs, cert_pem_begin_end);
  suite_add_tcase(s, tc_certs);

  TCase *tc_ssl = tcase_create("ssl");
  tcase_add_test(tc_ssl, ssl_connect_pcloud);
  tcase_add_test(tc_ssl, ssl_connect_untrusted_fails);
  suite_add_tcase(s, tc_ssl);

  TCase *tc_api = tcase_create("api");
  tcase_add_test(tc_api, api_currentserver);
  tcase_add_test(tc_api, api_getip);
  tcase_add_test(tc_api, api_getapiserver);
  suite_add_tcase(s, tc_api);

  return s;
}

int main(void) {
  provider_init();
  Suite *s = network_wolfssl_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
