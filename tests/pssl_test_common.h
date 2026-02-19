/* pssl_test_common.h — shared test cases for all SSL provider tests.
 *
 * This header is #included by each provider driver after the provider macro
 * (P_SSL_OPENSSL, P_SSL_WOLFSSL, or P_SSL_MBEDTLS) is defined so that
 * "pssl.h" resolves to the correct backend types and macros.
 */

#ifndef PSSL_TEST_COMMON_H
#define PSSL_TEST_COMMON_H

#include <check.h>
#include "pssl.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

/* ── SHA-1 ───────────────────────────────────────────────────────────────── */

/* RFC 3174: SHA1("abc") */
static const unsigned char sha1_abc_expected[PSYNC_SHA1_DIGEST_LEN] = {
  0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
  0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d
};

START_TEST(sha1_known_answer) {
  unsigned char digest[PSYNC_SHA1_DIGEST_LEN];
  psync_sha1((const unsigned char *)"abc", 3, digest);
  ck_assert(memcmp(digest, sha1_abc_expected, PSYNC_SHA1_DIGEST_LEN) == 0);
} END_TEST

START_TEST(sha1_streaming) {
  unsigned char oneshot[PSYNC_SHA1_DIGEST_LEN];
  unsigned char streamed[PSYNC_SHA1_DIGEST_LEN];
  psync_sha1_ctx ctx;
  const unsigned char *data = (const unsigned char *)"hello world";
  size_t len = 11;

  psync_sha1(data, len, oneshot);

  psync_sha1_init(&ctx);
  psync_sha1_update(&ctx, data, 5);
  psync_sha1_update(&ctx, data + 5, len - 5);
  psync_sha1_final(streamed, &ctx);

  ck_assert(memcmp(oneshot, streamed, PSYNC_SHA1_DIGEST_LEN) == 0);
} END_TEST

/* ── SHA-512 ─────────────────────────────────────────────────────────────── */

/* NIST FIPS 180-4: SHA-512("abc") */
static const unsigned char sha512_abc_expected[PSYNC_SHA512_DIGEST_LEN] = {
  0xdd,0xaf,0x35,0xa1,0x93,0x61,0x7a,0xba,0xcc,0x41,0x73,0x49,0xae,0x20,0x41,0x31,
  0x12,0xe6,0xfa,0x4e,0x89,0xa9,0x7e,0xa2,0x0a,0x9e,0xee,0xe6,0x4b,0x55,0xd3,0x9a,
  0x21,0x92,0x99,0x2a,0x27,0x4f,0xc1,0xa8,0x36,0xba,0x3c,0x23,0xa3,0xfe,0xeb,0xbd,
  0x45,0x4d,0x44,0x23,0x64,0x3c,0xe8,0x0e,0x2a,0x9a,0xc9,0x4f,0xa5,0x4c,0xa4,0x9f
};

START_TEST(sha512_known_answer) {
  unsigned char digest[PSYNC_SHA512_DIGEST_LEN];
  psync_sha512((const unsigned char *)"abc", 3, digest);
  ck_assert(memcmp(digest, sha512_abc_expected, PSYNC_SHA512_DIGEST_LEN) == 0);
} END_TEST

START_TEST(sha512_streaming) {
  unsigned char oneshot[PSYNC_SHA512_DIGEST_LEN];
  unsigned char streamed[PSYNC_SHA512_DIGEST_LEN];
  psync_sha512_ctx ctx;
  const unsigned char *data = (const unsigned char *)"hello world";
  size_t len = 11;

  psync_sha512(data, len, oneshot);

  psync_sha512_init(&ctx);
  psync_sha512_update(&ctx, data, 6);
  psync_sha512_update(&ctx, data + 6, len - 6);
  psync_sha512_final(streamed, &ctx);

  ck_assert(memcmp(oneshot, streamed, PSYNC_SHA512_DIGEST_LEN) == 0);
} END_TEST

/* ── AES-256 ─────────────────────────────────────────────────────────────── */

/* NIST AES-256 test key (FIPS 197, Appendix B extended) */
static const unsigned char aes_test_raw_key[PSYNC_AES256_KEY_SIZE] = {
  0x60,0x3d,0xeb,0x10,0x15,0xca,0x71,0xbe,
  0x2b,0x73,0xae,0xf0,0x85,0x7d,0x77,0x81,
  0x1f,0x35,0x2c,0x07,0x3b,0x61,0x08,0xd7,
  0x2d,0x98,0x10,0xa3,0x09,0x14,0xdf,0xf4
};

static psync_symmetric_key_t make_test_key(void) {
  psync_symmetric_key_t k = malloc(
    offsetof(psync_symmetric_key_struct_t, key) + PSYNC_AES256_KEY_SIZE);
  k->keylen = PSYNC_AES256_KEY_SIZE;
  memcpy(k->key, aes_test_raw_key, PSYNC_AES256_KEY_SIZE);
  return k;
}

START_TEST(aes256_encode_decode) {
  psync_symmetric_key_t key = make_test_key();
  psync_aes256_encoder enc;
  psync_aes256_encoder dec;
  unsigned char plain[PSYNC_AES256_BLOCK_SIZE] = {
    0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
    0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a
  };
  unsigned char ciphertext[PSYNC_AES256_BLOCK_SIZE];
  unsigned char recovered[PSYNC_AES256_BLOCK_SIZE];

  enc = psync_ssl_aes256_create_encoder(key);
  ck_assert_ptr_nonnull(enc);
  dec = psync_ssl_aes256_create_decoder(key);
  ck_assert_ptr_nonnull(dec);

  psync_aes256_encode_block(enc, plain, ciphertext);
  psync_aes256_decode_block(dec, ciphertext, recovered);

  ck_assert(memcmp(plain, recovered, PSYNC_AES256_BLOCK_SIZE) == 0);
  /* ciphertext must differ from plaintext for a non-trivial key */
  ck_assert(!(memcmp(plain, ciphertext, PSYNC_AES256_BLOCK_SIZE) == 0));

  psync_ssl_aes256_free_encoder(enc);
  psync_ssl_aes256_free_decoder(dec);
  free(key);
} END_TEST

START_TEST(aes256_2block_roundtrip) {
  psync_symmetric_key_t key = make_test_key();
  psync_aes256_encoder enc;
  psync_aes256_encoder dec;
  unsigned char plain[PSYNC_AES256_BLOCK_SIZE * 2];
  unsigned char ciphertext[PSYNC_AES256_BLOCK_SIZE * 2];
  unsigned char recovered[PSYNC_AES256_BLOCK_SIZE * 2];
  int i;

  for (i = 0; i < PSYNC_AES256_BLOCK_SIZE * 2; i++)
    plain[i] = (unsigned char)i;

  enc = psync_ssl_aes256_create_encoder(key);
  dec = psync_ssl_aes256_create_decoder(key);

  psync_aes256_encode_2blocks_consec(enc, plain, ciphertext);
  psync_aes256_decode_2blocks_consec(dec, ciphertext, recovered);

  ck_assert(memcmp(plain, recovered, PSYNC_AES256_BLOCK_SIZE * 2) == 0);

  psync_ssl_aes256_free_encoder(enc);
  psync_ssl_aes256_free_decoder(dec);
  free(key);
} END_TEST

/* ── RSA ─────────────────────────────────────────────────────────────────── */

START_TEST(rsa_gen_not_null) {
  psync_rsa_t rsa = psync_ssl_gen_rsa(2048);
  ck_assert_ptr_ne(rsa, PSYNC_INVALID_RSA);
  psync_ssl_free_rsa(rsa);
} END_TEST

START_TEST(rsa_public_binary_roundtrip) {
  psync_rsa_t rsa = psync_ssl_gen_rsa(2048);
  ck_assert_ptr_ne(rsa, PSYNC_INVALID_RSA);

  psync_rsa_publickey_t pub = psync_ssl_rsa_get_public(rsa);
  ck_assert_ptr_nonnull(pub);

  psync_binary_rsa_key_t bin = psync_ssl_rsa_public_to_binary(pub);
  ck_assert_ptr_nonnull(bin);

  psync_rsa_publickey_t pub2 = psync_ssl_rsa_binary_to_public(bin);
  ck_assert_ptr_nonnull(pub2);

  psync_ssl_rsa_free_binary(bin);
  psync_ssl_rsa_free_public(pub);
  psync_ssl_rsa_free_public(pub2);
  psync_ssl_free_rsa(rsa);
} END_TEST

START_TEST(rsa_private_binary_roundtrip) {
  psync_rsa_t rsa = psync_ssl_gen_rsa(2048);
  ck_assert_ptr_ne(rsa, PSYNC_INVALID_RSA);

  psync_rsa_privatekey_t priv = psync_ssl_rsa_get_private(rsa);
  ck_assert_ptr_nonnull(priv);

  psync_binary_rsa_key_t bin = psync_ssl_rsa_private_to_binary(priv);
  ck_assert_ptr_nonnull(bin);

  psync_rsa_privatekey_t priv2 = psync_ssl_rsa_binary_to_private(bin);
  ck_assert_ptr_nonnull(priv2);

  psync_ssl_rsa_free_binary(bin);
  psync_ssl_rsa_free_private(priv);
  psync_ssl_rsa_free_private(priv2);
  psync_ssl_free_rsa(rsa);
} END_TEST

START_TEST(rsa_encrypt_decrypt) {
  psync_rsa_t rsa = psync_ssl_gen_rsa(2048);
  ck_assert_ptr_ne(rsa, PSYNC_INVALID_RSA);

  psync_rsa_publickey_t pub = psync_ssl_rsa_get_public(rsa);
  psync_rsa_privatekey_t priv = psync_ssl_rsa_get_private(rsa);

  unsigned char plaintext[32];
  psync_ssl_rand_strong(plaintext, sizeof(plaintext));

  psync_encrypted_symmetric_key_t enc =
    psync_ssl_rsa_encrypt_data(pub, plaintext, sizeof(plaintext));
  ck_assert_ptr_nonnull(enc);

  psync_symmetric_key_t dec =
    psync_ssl_rsa_decrypt_data(priv, enc->data, enc->datalen);
  ck_assert_ptr_nonnull(dec);
  ck_assert_int_eq((int)dec->keylen, (int)sizeof(plaintext));
  ck_assert(memcmp(dec->key, plaintext, sizeof(plaintext)) == 0);

  /* enc was allocated with psync_malloc, stubbed to malloc in tests */
  free(enc);
  psync_ssl_free_symmetric_key(dec);
  psync_ssl_rsa_free_public(pub);
  psync_ssl_rsa_free_private(priv);
  psync_ssl_free_rsa(rsa);
} END_TEST

/* ── PBKDF2 ──────────────────────────────────────────────────────────────── */

START_TEST(pbkdf2_deterministic) {
  static const unsigned char salt[16] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10
  };
  psync_symmetric_key_t k1 =
    psync_ssl_gen_symmetric_key_from_pass("passphrase", 32, salt, sizeof(salt), 1000);
  psync_symmetric_key_t k2 =
    psync_ssl_gen_symmetric_key_from_pass("passphrase", 32, salt, sizeof(salt), 1000);

  ck_assert_ptr_nonnull(k1);
  ck_assert_ptr_nonnull(k2);
  ck_assert(memcmp(k1->key, k2->key, 32) == 0);

  psync_ssl_free_symmetric_key(k1);
  psync_ssl_free_symmetric_key(k2);
} END_TEST

START_TEST(pbkdf2_differs_on_salt) {
  static const unsigned char salt1[16] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10
  };
  static const unsigned char salt2[16] = {
    0xff,0xfe,0xfd,0xfc,0xfb,0xfa,0xf9,0xf8,
    0xf7,0xf6,0xf5,0xf4,0xf3,0xf2,0xf1,0xf0
  };
  psync_symmetric_key_t k1 =
    psync_ssl_gen_symmetric_key_from_pass("passphrase", 32, salt1, sizeof(salt1), 1000);
  psync_symmetric_key_t k2 =
    psync_ssl_gen_symmetric_key_from_pass("passphrase", 32, salt2, sizeof(salt2), 1000);

  ck_assert_ptr_nonnull(k1);
  ck_assert_ptr_nonnull(k2);
  ck_assert(!(memcmp(k1->key, k2->key, 32) == 0));

  psync_ssl_free_symmetric_key(k1);
  psync_ssl_free_symmetric_key(k2);
} END_TEST

/* ── Random ──────────────────────────────────────────────────────────────── */

START_TEST(rand_strong_not_all_zero) {
  unsigned char buf[32];
  int i;
  memset(buf, 0, sizeof(buf));
  psync_ssl_rand_strong(buf, sizeof(buf));
  for (i = 0; i < (int)sizeof(buf); i++)
    if (buf[i] != 0) return;  /* at least one non-zero byte — pass */
  ck_assert(0);  /* all zero — fail */
} END_TEST

START_TEST(rand_weak_not_all_zero) {
  unsigned char buf[32];
  int i;
  memset(buf, 0, sizeof(buf));
  psync_ssl_rand_weak(buf, sizeof(buf));
  for (i = 0; i < (int)sizeof(buf); i++)
    if (buf[i] != 0) return;
  ck_assert(0);
} END_TEST

#endif /* PSSL_TEST_COMMON_H */
