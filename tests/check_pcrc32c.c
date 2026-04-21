#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "pcrc32c.h"

/* --- CRC32C tests --- */

START_TEST(test_crc32c_empty) {
  uint32_t crc = psync_crc32c(PSYNC_CRC_INITIAL, "", 0);
  ck_assert_uint_eq(crc, 0);
}
END_TEST

START_TEST(test_crc32c_known_vector) {
  /* Known test vector for this implementation */
  const char *input = "123456789";
  uint32_t crc = psync_crc32c(PSYNC_CRC_INITIAL, input, 9);
  ck_assert_uint_eq(crc, 0x58E3FA20);
}
END_TEST

START_TEST(test_crc32c_incremental_vs_whole) {
  const char *data = "Hello, World! This is a test string for CRC32C.";
  size_t len = strlen(data);

  uint32_t whole = psync_crc32c(PSYNC_CRC_INITIAL, data, len);

  /* Compute incrementally in chunks */
  uint32_t incremental = PSYNC_CRC_INITIAL;
  size_t chunk = 7;
  for (size_t i = 0; i < len; i += chunk) {
    size_t n = (i + chunk <= len) ? chunk : len - i;
    incremental = psync_crc32c(incremental, data + i, n);
  }

  ck_assert_uint_eq(incremental, whole);
}
END_TEST

START_TEST(test_crc32c_different_inputs_differ) {
  uint32_t crc1 = psync_crc32c(PSYNC_CRC_INITIAL, "abc", 3);
  uint32_t crc2 = psync_crc32c(PSYNC_CRC_INITIAL, "abd", 3);
  ck_assert_uint_ne(crc1, crc2);
}
END_TEST

START_TEST(test_crc32c_alignment) {
  /* Test that unaligned data works correctly */
  char buf[20];
  memset(buf, 'A', sizeof(buf));

  uint32_t aligned = psync_crc32c(PSYNC_CRC_INITIAL, buf, 16);
  /* Same data at offset 1 (unaligned) */
  uint32_t unaligned = psync_crc32c(PSYNC_CRC_INITIAL, buf + 1, 16);
  /* Both are 16 bytes of 'A' so should be equal */
  ck_assert_uint_eq(aligned, unaligned);
}
END_TEST

START_TEST(test_crc32c_large_buffer) {
  size_t sz = 64 * 1024;
  char *buf = malloc(sz);
  ck_assert_ptr_ne(buf, NULL);
  memset(buf, 0x42, sz);

  uint32_t crc1 = psync_crc32c(PSYNC_CRC_INITIAL, buf, sz);
  uint32_t crc2 = psync_crc32c(PSYNC_CRC_INITIAL, buf, sz);
  ck_assert_uint_eq(crc1, crc2);
  /* Should be non-zero for non-trivial data */
  ck_assert_uint_ne(crc1, 0);

  free(buf);
}
END_TEST

/* --- Fast hash 256 tests --- */

START_TEST(test_hash256_determinism) {
  const char *data = "test data for hashing";
  unsigned char hash1[PSYNC_FAST_HASH256_LEN];
  unsigned char hash2[PSYNC_FAST_HASH256_LEN];

  psync_fast_hash256_ctx ctx;
  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, data, strlen(data));
  psync_fast_hash256_final(hash1, &ctx);

  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, data, strlen(data));
  psync_fast_hash256_final(hash2, &ctx);

  ck_assert_mem_eq(hash1, hash2, PSYNC_FAST_HASH256_LEN);
}
END_TEST

START_TEST(test_hash256_different_inputs) {
  unsigned char hash1[PSYNC_FAST_HASH256_LEN];
  unsigned char hash2[PSYNC_FAST_HASH256_LEN];
  psync_fast_hash256_ctx ctx;

  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, "input A", 7);
  psync_fast_hash256_final(hash1, &ctx);

  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, "input B", 7);
  psync_fast_hash256_final(hash2, &ctx);

  ck_assert_mem_ne(hash1, hash2, PSYNC_FAST_HASH256_LEN);
}
END_TEST

START_TEST(test_hash256_output_length) {
  unsigned char hash[PSYNC_FAST_HASH256_LEN + 8];
  memset(hash, 0xAA, sizeof(hash));

  psync_fast_hash256_ctx ctx;
  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, "test", 4);
  psync_fast_hash256_final(hash, &ctx);

  /* Sentinel bytes after the hash should be untouched */
  for (int i = PSYNC_FAST_HASH256_LEN; i < PSYNC_FAST_HASH256_LEN + 8; i++)
    ck_assert_uint_eq(hash[i], 0xAA);
}
END_TEST

START_TEST(test_hash256_incremental_vs_whole) {
  const char *data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz!@#$%^&*()_+-=";
  size_t len = strlen(data);
  unsigned char whole[PSYNC_FAST_HASH256_LEN];
  unsigned char incremental[PSYNC_FAST_HASH256_LEN];
  psync_fast_hash256_ctx ctx;

  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, data, len);
  psync_fast_hash256_final(whole, &ctx);

  psync_fast_hash256_init(&ctx);
  size_t chunk = 11;
  for (size_t i = 0; i < len; i += chunk) {
    size_t n = (i + chunk <= len) ? chunk : len - i;
    psync_fast_hash256_update(&ctx, data + i, n);
  }
  psync_fast_hash256_final(incremental, &ctx);

  ck_assert_mem_eq(whole, incremental, PSYNC_FAST_HASH256_LEN);
}
END_TEST

START_TEST(test_hash256_seeded_vs_unseeded) {
  const char *data = "same data";
  unsigned char hash_unseeded[PSYNC_FAST_HASH256_LEN];
  unsigned char hash_seeded[PSYNC_FAST_HASH256_LEN];
  psync_fast_hash256_ctx ctx;

  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, data, strlen(data));
  psync_fast_hash256_final(hash_unseeded, &ctx);

  const char *seed = "myseed";
  psync_fast_hash256_init_seed(&ctx, seed, strlen(seed));
  psync_fast_hash256_update(&ctx, data, strlen(data));
  psync_fast_hash256_final(hash_seeded, &ctx);

  ck_assert_mem_ne(hash_unseeded, hash_seeded, PSYNC_FAST_HASH256_LEN);
}
END_TEST

START_TEST(test_hash256_different_seeds) {
  const char *data = "same data";
  unsigned char hash1[PSYNC_FAST_HASH256_LEN];
  unsigned char hash2[PSYNC_FAST_HASH256_LEN];
  psync_fast_hash256_ctx ctx;

  psync_fast_hash256_init_seed(&ctx, "seed1", 5);
  psync_fast_hash256_update(&ctx, data, strlen(data));
  psync_fast_hash256_final(hash1, &ctx);

  psync_fast_hash256_init_seed(&ctx, "seed2", 5);
  psync_fast_hash256_update(&ctx, data, strlen(data));
  psync_fast_hash256_final(hash2, &ctx);

  ck_assert_mem_ne(hash1, hash2, PSYNC_FAST_HASH256_LEN);
}
END_TEST

START_TEST(test_hash256_empty_input) {
  unsigned char hash[PSYNC_FAST_HASH256_LEN];
  psync_fast_hash256_ctx ctx;

  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_final(hash, &ctx);

  /* Should produce a valid hash (not all zeros necessarily, but deterministic) */
  unsigned char hash2[PSYNC_FAST_HASH256_LEN];
  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_final(hash2, &ctx);

  ck_assert_mem_eq(hash, hash2, PSYNC_FAST_HASH256_LEN);
}
END_TEST

START_TEST(test_hash256_large_input) {
  size_t sz = 256 * 1024;
  char *buf = malloc(sz);
  ck_assert_ptr_ne(buf, NULL);
  for (size_t i = 0; i < sz; i++)
    buf[i] = (char)(i & 0xFF);

  unsigned char hash1[PSYNC_FAST_HASH256_LEN];
  unsigned char hash2[PSYNC_FAST_HASH256_LEN];
  psync_fast_hash256_ctx ctx;

  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, buf, sz);
  psync_fast_hash256_final(hash1, &ctx);

  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, buf, sz);
  psync_fast_hash256_final(hash2, &ctx);

  ck_assert_mem_eq(hash1, hash2, PSYNC_FAST_HASH256_LEN);
  free(buf);
}
END_TEST

START_TEST(test_hash256_block_boundary) {
  /* Test with data exactly at block boundary (64 bytes) */
  char data[PSYNC_FAST_HASH256_BLOCK_LEN];
  memset(data, 'X', sizeof(data));

  unsigned char hash1[PSYNC_FAST_HASH256_LEN];
  unsigned char hash2[PSYNC_FAST_HASH256_LEN];
  psync_fast_hash256_ctx ctx;

  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, data, PSYNC_FAST_HASH256_BLOCK_LEN);
  psync_fast_hash256_final(hash1, &ctx);

  /* Same data in two halves */
  psync_fast_hash256_init(&ctx);
  psync_fast_hash256_update(&ctx, data, PSYNC_FAST_HASH256_BLOCK_LEN / 2);
  psync_fast_hash256_update(&ctx, data + PSYNC_FAST_HASH256_BLOCK_LEN / 2, PSYNC_FAST_HASH256_BLOCK_LEN / 2);
  psync_fast_hash256_final(hash2, &ctx);

  ck_assert_mem_eq(hash1, hash2, PSYNC_FAST_HASH256_LEN);
}
END_TEST

/* --- Suite --- */

static Suite *pcrc32c_suite(void) {
  Suite *s = suite_create("pcrc32c");

  TCase *tc_crc = tcase_create("crc32c");
  tcase_add_test(tc_crc, test_crc32c_empty);
  tcase_add_test(tc_crc, test_crc32c_known_vector);
  tcase_add_test(tc_crc, test_crc32c_incremental_vs_whole);
  tcase_add_test(tc_crc, test_crc32c_different_inputs_differ);
  tcase_add_test(tc_crc, test_crc32c_alignment);
  tcase_add_test(tc_crc, test_crc32c_large_buffer);
  suite_add_tcase(s, tc_crc);

  TCase *tc_hash = tcase_create("fast_hash256");
  tcase_add_test(tc_hash, test_hash256_determinism);
  tcase_add_test(tc_hash, test_hash256_different_inputs);
  tcase_add_test(tc_hash, test_hash256_output_length);
  tcase_add_test(tc_hash, test_hash256_incremental_vs_whole);
  tcase_add_test(tc_hash, test_hash256_seeded_vs_unseeded);
  tcase_add_test(tc_hash, test_hash256_different_seeds);
  tcase_add_test(tc_hash, test_hash256_empty_input);
  tcase_add_test(tc_hash, test_hash256_large_input);
  tcase_add_test(tc_hash, test_hash256_block_boundary);
  suite_add_tcase(s, tc_hash);

  return s;
}

int main(void) {
  Suite *s = pcrc32c_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
