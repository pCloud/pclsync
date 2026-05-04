/* OpenSSL provider test driver (Check framework).
 *
 * Build:  make check-pssl-openssl   (requires OpenSSL 1.1.x or later)
 * Run:    ./tests/check_pssl_openssl
 */

#include <check.h>
#include <stdlib.h>

/* Pull in the OpenSSL backend types / SHA macros.
 * The compiler also receives -DP_SSL_OPENSSL from TEST_PSSL_CFLAGS; the
 * ifndef guard here avoids a redefinition warning when that flag is passed. */
#ifndef P_SSL_OPENSSL
#define P_SSL_OPENSSL
#endif
#include "pssl.h"
/* The local tests/pencoding.h is an empty shim; pull the real header
 * directly so the psync_binhex macro is visible. */
#include "../pencoding.h"
#include "psslcerts.h"
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

/* Seed OpenSSL's PRNG from /dev/urandom before any test runs.
 * OpenSSL 1.1+ auto-seeds on most platforms, but seeding explicitly
 * guarantees RAND_status() returns 1 even in minimal environments. */
static void provider_init(void) {
  unsigned char seed[64];
  FILE *f = fopen("/dev/urandom", "rb");
  if (f) { fread(seed, 1, sizeof(seed), f); fclose(f); }
  RAND_seed(seed, sizeof(seed));
}

/* Include shared test cases — they reference the macros/types resolved above */
#include "pssl_test_common.h"

/* SPKI fingerprint extraction regression test.
 *
 * The static helper psync_ssl_check_peer_public_key() in pssl-openssl.c is
 * not directly callable from a different translation unit, so we exercise
 * the same primitive chain (X509_get_X509_PUBKEY -> i2d_X509_PUBKEY ->
 * psync_sha256 -> psync_binhex) against a known certificate and assert a
 * hard-coded SHA-256.  This catches accidental swaps such as substituting
 * i2d_PUBKEY for i2d_X509_PUBKEY, which would silently break pin matching
 * against the production fingerprints in psync_ssl_trusted_pk_sha256[]. */
START_TEST(spki_extraction_known_answer) {
  const char *pem = psync_ssl_trusted_certs[0];
  BIO *bio = NULL;
  X509 *cert = NULL;
  X509_PUBKEY *pubkey; /* borrowed from cert; do not free */
  unsigned char *der = NULL;
  int derlen = 0;
  unsigned char digest[PSYNC_SHA256_DIGEST_LEN];
  char hex[PSYNC_SHA256_DIGEST_HEXLEN + 1];
  int rc = -1;

  bio = BIO_new(BIO_s_mem());
  if (!bio) goto out;
  if (BIO_puts(bio, pem) <= 0) goto out;

  cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
  if (!cert) goto out;

  pubkey = X509_get_X509_PUBKEY(cert);
  if (!pubkey) goto out;

  derlen = i2d_X509_PUBKEY(pubkey, &der);
  if (derlen <= 0 || !der) goto out;

  psync_sha256(der, (size_t)derlen, digest);
  psync_binhex(hex, digest, PSYNC_SHA256_DIGEST_LEN);
  hex[PSYNC_SHA256_DIGEST_HEXLEN] = 0;
  rc = 0;

out:
  OPENSSL_free(der);   /* OPENSSL_free(NULL) is a no-op */
  X509_free(cert);     /* X509_free(NULL) is a no-op */
  BIO_free(bio);       /* BIO_free(NULL) is a no-op */

  /* Assertions are deferred to here so a longjmp out of any ck_assert_*
   * cannot leak the OpenSSL resources above.  rc==0 implies hex is
   * fully populated; if rc!=0 the rc assertion fails first and the
   * subsequent str_eq is never evaluated. */
  ck_assert_int_eq(rc, 0);
  ck_assert_str_eq(hex,
    "9318226f8c83afe47f5f47c24f59ce12dba8c73b181bee6b2ea1f40a06bc1869");
} END_TEST

static Suite *pssl_openssl_suite(void) {
  Suite *s = suite_create("pssl_openssl");

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

  TCase *tc_spki = tcase_create("SPKI");
  tcase_add_test(tc_spki, spki_extraction_known_answer);
  suite_add_tcase(s, tc_spki);

  return s;
}

int main(void) {
  provider_init();

  Suite *s = pssl_openssl_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
