#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "pcompression.h"

/* --- Helpers --- */

/* Compress data, then decompress and verify roundtrip. */
static void roundtrip(int level, const void *input, int input_len) {
  psync_deflate_t *comp, *decomp;
  unsigned char cbuf[16384], dbuf[16384];
  int written, total_compressed, total_decompressed;
  int rd;

  comp = psync_deflate_init(level);
  ck_assert_ptr_ne(comp, NULL);

  /* Write all input with FLUSH_END */
  written = psync_deflate_write(comp, input, input_len, PSYNC_DEFLATE_FLUSH_END);
  ck_assert_int_eq(written, input_len);

  /* Read compressed output */
  total_compressed = 0;
  while (1) {
    rd = psync_deflate_read(comp, cbuf + total_compressed,
                            (int)sizeof(cbuf) - total_compressed);
    if (rd == PSYNC_DEFLATE_NODATA || rd == PSYNC_DEFLATE_EOF)
      break;
    ck_assert_int_gt(rd, 0);
    total_compressed += rd;
  }
  ck_assert_int_gt(total_compressed, 0);

  psync_deflate_destroy(comp);

  /* Decompress — feed compressed data, reading output between writes */
  decomp = psync_deflate_init(PSYNC_DEFLATE_DECOMPRESS);
  ck_assert_ptr_ne(decomp, NULL);

  total_decompressed = 0;
  {
    int fed = 0;
    while (fed < total_compressed) {
      written = psync_deflate_write(decomp, cbuf + fed,
                                    total_compressed - fed, PSYNC_DEFLATE_NOFLUSH);
      if (written == PSYNC_DEFLATE_FULL) {
        /* Drain some output to make room */
        rd = psync_deflate_read(decomp, dbuf + total_decompressed,
                                (int)sizeof(dbuf) - total_decompressed);
        ck_assert_int_gt(rd, 0);
        total_decompressed += rd;
        continue;
      }
      ck_assert_int_gt(written, 0);
      fed += written;
      /* Read any available output */
      while (1) {
        rd = psync_deflate_read(decomp, dbuf + total_decompressed,
                                (int)sizeof(dbuf) - total_decompressed);
        if (rd == PSYNC_DEFLATE_NODATA || rd == PSYNC_DEFLATE_EOF)
          break;
        ck_assert_int_gt(rd, 0);
        total_decompressed += rd;
      }
    }
    /* Drain remaining output */
    while (1) {
      rd = psync_deflate_read(decomp, dbuf + total_decompressed,
                              (int)sizeof(dbuf) - total_decompressed);
      if (rd == PSYNC_DEFLATE_NODATA || rd == PSYNC_DEFLATE_EOF)
        break;
      ck_assert_int_gt(rd, 0);
      total_decompressed += rd;
    }
  }

  ck_assert_int_eq(total_decompressed, input_len);
  ck_assert_mem_eq(dbuf, input, input_len);

  psync_deflate_destroy(decomp);
}

/* --- Tests --- */

/* init / destroy */

START_TEST(test_init_compress) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  ck_assert_ptr_ne(def, NULL);
  ck_assert_int_eq(psync_deflate_pending(def), 0);
  psync_deflate_destroy(def);
}
END_TEST

START_TEST(test_init_decompress) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_DECOMPRESS);
  ck_assert_ptr_ne(def, NULL);
  ck_assert_int_eq(psync_deflate_pending(def), 0);
  psync_deflate_destroy(def);
}
END_TEST

/* roundtrip tests */

START_TEST(test_roundtrip_small) {
  const char *msg = "Hello, pCloud!";
  roundtrip(PSYNC_DEFLATE_COMP_FAST, msg, (int)strlen(msg));
}
END_TEST

START_TEST(test_roundtrip_best) {
  const char *msg = "Hello, pCloud!";
  roundtrip(PSYNC_DEFLATE_COMP_BEST, msg, (int)strlen(msg));
}
END_TEST

START_TEST(test_roundtrip_medium) {
  const char *msg = "The quick brown fox jumps over the lazy dog. "
                    "Pack my box with five dozen liquor jugs.";
  roundtrip(PSYNC_DEFLATE_COMP_MED, msg, (int)strlen(msg));
}
END_TEST

START_TEST(test_roundtrip_binary) {
  unsigned char data[256];
  int i;
  for (i = 0; i < 256; i++)
    data[i] = (unsigned char)i;
  roundtrip(PSYNC_DEFLATE_COMP_FAST, data, 256);
}
END_TEST

START_TEST(test_roundtrip_large) {
  /* 8KB of repeated pattern — tests buffer wrapping */
  unsigned char data[8192];
  int i;
  for (i = 0; i < (int)sizeof(data); i++)
    data[i] = (unsigned char)(i % 251);
  roundtrip(PSYNC_DEFLATE_COMP_FAST, data, (int)sizeof(data));
}
END_TEST

START_TEST(test_roundtrip_highly_compressible) {
  /* All zeros — tests the flush buffer growth path */
  unsigned char data[8192];
  memset(data, 0, sizeof(data));
  roundtrip(PSYNC_DEFLATE_COMP_BEST, data, (int)sizeof(data));
}
END_TEST

/* write / read API */

START_TEST(test_write_returns_bytes_consumed) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  const char *msg = "test data";
  int written = psync_deflate_write(def, msg, 9, PSYNC_DEFLATE_FLUSH);
  ck_assert_int_eq(written, 9);
  psync_deflate_destroy(def);
}
END_TEST

START_TEST(test_write_no_data_no_flush_is_error) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  int ret = psync_deflate_write(def, "", 0, PSYNC_DEFLATE_NOFLUSH);
  ck_assert_int_eq(ret, PSYNC_DEFLATE_ERROR);
  psync_deflate_destroy(def);
}
END_TEST

START_TEST(test_read_empty_returns_nodata) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  unsigned char buf[64];
  int rd = psync_deflate_read(def, buf, (int)sizeof(buf));
  ck_assert_int_eq(rd, PSYNC_DEFLATE_NODATA);
  psync_deflate_destroy(def);
}
END_TEST

/* pending */

START_TEST(test_pending_increases_after_write) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  const char *data = "some data to compress for pending test";
  psync_deflate_write(def, data, (int)strlen(data), PSYNC_DEFLATE_FLUSH);
  ck_assert_int_gt(psync_deflate_pending(def), 0);
  psync_deflate_destroy(def);
}
END_TEST

START_TEST(test_pending_decreases_after_read) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  unsigned char buf[4096];
  const char *data = "some data to compress for pending test";
  int pending_before, pending_after;

  psync_deflate_write(def, data, (int)strlen(data), PSYNC_DEFLATE_FLUSH);
  pending_before = psync_deflate_pending(def);
  ck_assert_int_gt(pending_before, 0);

  psync_deflate_read(def, buf, (int)sizeof(buf));
  pending_after = psync_deflate_pending(def);
  ck_assert_int_lt(pending_after, pending_before);
  psync_deflate_destroy(def);
}
END_TEST

START_TEST(test_pending_zero_after_drain) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  unsigned char buf[4096];
  const char *data = "drain test data";
  int rd;

  psync_deflate_write(def, data, (int)strlen(data), PSYNC_DEFLATE_FLUSH_END);

  /* Drain all output */
  while (1) {
    rd = psync_deflate_read(def, buf, (int)sizeof(buf));
    if (rd == PSYNC_DEFLATE_NODATA || rd == PSYNC_DEFLATE_EOF)
      break;
  }
  ck_assert_int_eq(psync_deflate_pending(def), 0);
  psync_deflate_destroy(def);
}
END_TEST

/* flush modes */

START_TEST(test_flush_partial) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  unsigned char buf[4096];
  const char *data = "partial flush test";
  int written, rd;

  written = psync_deflate_write(def, data, (int)strlen(data), PSYNC_DEFLATE_FLUSH);
  ck_assert_int_eq(written, (int)strlen(data));

  /* Should be able to read flushed output */
  rd = psync_deflate_read(def, buf, (int)sizeof(buf));
  ck_assert_int_gt(rd, 0);

  /* Can still write more after partial flush */
  written = psync_deflate_write(def, data, (int)strlen(data), PSYNC_DEFLATE_FLUSH);
  ck_assert_int_eq(written, (int)strlen(data));

  psync_deflate_destroy(def);
}
END_TEST

START_TEST(test_flush_end_then_eof) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  unsigned char buf[4096];
  const char *data = "eof test";
  int rd;

  psync_deflate_write(def, data, (int)strlen(data), PSYNC_DEFLATE_FLUSH_END);

  /* Drain */
  while (1) {
    rd = psync_deflate_read(def, buf, (int)sizeof(buf));
    if (rd <= 0)
      break;
  }
  /* After FLUSH_END and drain, should get EOF */
  rd = psync_deflate_read(def, buf, (int)sizeof(buf));
  ck_assert_int_eq(rd, PSYNC_DEFLATE_EOF);

  psync_deflate_destroy(def);
}
END_TEST

/* incremental write/read */

START_TEST(test_incremental_write_read) {
  psync_deflate_t *comp, *decomp;
  unsigned char cbuf[8192], dbuf[8192];
  const char *chunks[] = {"Hello ", "World ", "from ", "pCloud!"};
  char expected[64] = {0};
  int i, total_compressed = 0, total_decompressed = 0, rd;

  comp = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  ck_assert_ptr_ne(comp, NULL);

  /* Write chunks with partial flush */
  for (i = 0; i < 4; i++) {
    int flush = (i == 3) ? PSYNC_DEFLATE_FLUSH_END : PSYNC_DEFLATE_FLUSH;
    int written = psync_deflate_write(comp, chunks[i], (int)strlen(chunks[i]), flush);
    ck_assert_int_eq(written, (int)strlen(chunks[i]));
    strcat(expected, chunks[i]);
  }

  /* Read all compressed data */
  while (1) {
    rd = psync_deflate_read(comp, cbuf + total_compressed,
                            (int)sizeof(cbuf) - total_compressed);
    if (rd == PSYNC_DEFLATE_NODATA || rd == PSYNC_DEFLATE_EOF)
      break;
    ck_assert_int_gt(rd, 0);
    total_compressed += rd;
  }

  psync_deflate_destroy(comp);

  /* Decompress */
  decomp = psync_deflate_init(PSYNC_DEFLATE_DECOMPRESS);

  rd = psync_deflate_write(decomp, cbuf, total_compressed, PSYNC_DEFLATE_NOFLUSH);
  ck_assert_int_eq(rd, total_compressed);

  while (1) {
    rd = psync_deflate_read(decomp, dbuf + total_decompressed,
                            (int)sizeof(dbuf) - total_decompressed);
    if (rd == PSYNC_DEFLATE_NODATA || rd == PSYNC_DEFLATE_EOF)
      break;
    ck_assert_int_gt(rd, 0);
    total_decompressed += rd;
  }

  ck_assert_int_eq(total_decompressed, (int)strlen(expected));
  ck_assert_mem_eq(dbuf, expected, total_decompressed);

  psync_deflate_destroy(decomp);
}
END_TEST

/* small reads (reading less than available) */

START_TEST(test_small_reads) {
  psync_deflate_t *def = psync_deflate_init(PSYNC_DEFLATE_COMP_FAST);
  unsigned char buf[1];
  const char *data = "small read test data";
  int total = 0, rd;

  psync_deflate_write(def, data, (int)strlen(data), PSYNC_DEFLATE_FLUSH);

  /* Read one byte at a time */
  while (1) {
    rd = psync_deflate_read(def, buf, 1);
    if (rd == PSYNC_DEFLATE_NODATA || rd == PSYNC_DEFLATE_EOF)
      break;
    ck_assert_int_eq(rd, 1);
    total++;
  }
  ck_assert_int_gt(total, 0);

  psync_deflate_destroy(def);
}
END_TEST

/* --- Suite setup --- */

static Suite *pcompression_suite(void) {
  Suite *s = suite_create("pcompression");

  TCase *tc_init = tcase_create("init");
  tcase_add_test(tc_init, test_init_compress);
  tcase_add_test(tc_init, test_init_decompress);
  suite_add_tcase(s, tc_init);

  TCase *tc_roundtrip = tcase_create("roundtrip");
  tcase_add_test(tc_roundtrip, test_roundtrip_small);
  tcase_add_test(tc_roundtrip, test_roundtrip_best);
  tcase_add_test(tc_roundtrip, test_roundtrip_medium);
  tcase_add_test(tc_roundtrip, test_roundtrip_binary);
  tcase_add_test(tc_roundtrip, test_roundtrip_large);
  tcase_add_test(tc_roundtrip, test_roundtrip_highly_compressible);
  suite_add_tcase(s, tc_roundtrip);

  TCase *tc_api = tcase_create("api");
  tcase_add_test(tc_api, test_write_returns_bytes_consumed);
  tcase_add_test(tc_api, test_write_no_data_no_flush_is_error);
  tcase_add_test(tc_api, test_read_empty_returns_nodata);
  suite_add_tcase(s, tc_api);

  TCase *tc_pending = tcase_create("pending");
  tcase_add_test(tc_pending, test_pending_increases_after_write);
  tcase_add_test(tc_pending, test_pending_decreases_after_read);
  tcase_add_test(tc_pending, test_pending_zero_after_drain);
  suite_add_tcase(s, tc_pending);

  TCase *tc_flush = tcase_create("flush");
  tcase_add_test(tc_flush, test_flush_partial);
  tcase_add_test(tc_flush, test_flush_end_then_eof);
  suite_add_tcase(s, tc_flush);

  TCase *tc_incremental = tcase_create("incremental");
  tcase_add_test(tc_incremental, test_incremental_write_read);
  tcase_add_test(tc_incremental, test_small_reads);
  suite_add_tcase(s, tc_incremental);

  return s;
}

int main(void) {
  Suite *s = pcompression_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
