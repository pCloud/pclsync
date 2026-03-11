/* OpenSSL 3 provider test driver (Check framework).
 *
 * Build:  make check-pssl-openssl3   (requires OpenSSL 3.x)
 * Run:    ./tests/check_pssl_openssl3
 */

#include <check.h>
#include <stdlib.h>

/* Pull in the OpenSSL 3 backend types / SHA macros.
 * The compiler also receives -DP_SSL_OPENSSL3 from TEST_PSSL_CFLAGS; the
 * ifndef guard here avoids a redefinition warning when that flag is passed. */
#ifndef P_SSL_OPENSSL3
#define P_SSL_OPENSSL3
#endif
#include "pssl.h"
#include <openssl/rand.h>

/* Seed OpenSSL's PRNG from /dev/urandom before any test runs.
 * OpenSSL 3.x auto-seeds on most platforms, but seeding explicitly
 * guarantees RAND_status() returns 1 even in minimal environments. */
static void provider_init(void) {
  unsigned char seed[64];
  FILE *f = fopen("/dev/urandom", "rb");
  if (f) { fread(seed, 1, sizeof(seed), f); fclose(f); }
  RAND_seed(seed, sizeof(seed));
}

/* Include shared test cases — they reference the macros/types resolved above */
#include "pssl_test_common.h"

/* OpenSSL 3-specific test: verify that derive_password_from_passphrase works */
START_TEST(derive_password_not_null) {
  char *result = psync_ssl_derive_password_from_passphrase("user@example.com", "secret");
  ck_assert_ptr_nonnull(result);
  free(result);
} END_TEST

static Suite *pssl_openssl3_suite(void) {
  Suite *s = suite_create("pssl_openssl3");

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

  TCase *tc_openssl3 = tcase_create("OpenSSL3");
  tcase_add_test(tc_openssl3, derive_password_not_null);
  suite_add_tcase(s, tc_openssl3);

  return s;
}

int main(void) {
  provider_init();

  Suite *s = pssl_openssl3_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
