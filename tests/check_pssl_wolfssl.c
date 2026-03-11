/* WolfSSL provider test driver (Check framework).
 *
 * Build:  make check-pssl-wolfssl   (requires WolfSSL)
 * Run:    ./tests/check_pssl_wolfssl
 */

#include <check.h>
#include <stdlib.h>

/* Pull in the WolfSSL backend types / SHA macros. */
#ifndef P_SSL_WOLFSSL
#define P_SSL_WOLFSSL
#endif
#include "pssl.h"

static void provider_init(void) {
  /* psync_ssl_init() calls wolfSSL_Init(), seeds the RNG, and creates
   * the global WOLFSSL_CTX.  Even if the CTX step fails (no certs),
   * the RNG will be ready for the crypto tests. */
  psync_ssl_init();
}

/* Include shared test cases */
#include "pssl_test_common.h"

static Suite *pssl_wolfssl_suite(void) {
  Suite *s = suite_create("pssl_wolfssl");

  TCase *tc_sha = tcase_create("SHA");
  tcase_add_test(tc_sha, sha1_known_answer);
  tcase_add_test(tc_sha, sha1_streaming);
  tcase_add_test(tc_sha, sha512_known_answer);
  tcase_add_test(tc_sha, sha512_streaming);
  suite_add_tcase(s, tc_sha);

  TCase *tc_aes = tcase_create("AES");
  tcase_add_test(tc_aes, aes256_encode_decode);
  tcase_add_test(tc_aes, aes256_2block_roundtrip);
  suite_add_tcase(s, tc_aes);

  TCase *tc_rsa = tcase_create("RSA");
  tcase_set_timeout(tc_rsa, 60);
  tcase_add_test(tc_rsa, rsa_gen_not_null);
  tcase_add_test(tc_rsa, rsa_public_binary_roundtrip);
  tcase_add_test(tc_rsa, rsa_private_binary_roundtrip);
  tcase_add_test(tc_rsa, rsa_encrypt_decrypt);
  suite_add_tcase(s, tc_rsa);

  TCase *tc_pbkdf2 = tcase_create("PBKDF2");
  tcase_add_test(tc_pbkdf2, pbkdf2_deterministic);
  tcase_add_test(tc_pbkdf2, pbkdf2_differs_on_salt);
  suite_add_tcase(s, tc_pbkdf2);

  TCase *tc_random = tcase_create("Random");
  tcase_add_test(tc_random, rand_strong_not_all_zero);
  tcase_add_test(tc_random, rand_weak_not_all_zero);
  suite_add_tcase(s, tc_random);

  return s;
}

int main(void) {
  provider_init();

  Suite *s = pssl_wolfssl_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
