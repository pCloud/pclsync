/* Binary protocol unit tests for papi.c.
 *
 * Build:  make test   (included in TEST_BINS)
 * Run:    ./tests/check_papi
 *
 * Tests serialization (do_prepare_command) and deserialization (get_result,
 * psync_find_result, psync_check_result) of the pCloud binary protocol.
 */

#include <check.h>
#include <stdlib.h>
#include "papi.h"
#include "psettings.h"
#include <string.h>
#include <stdint.h>

/* -- Mock buffer from papi_test_stubs.c ----------------------------------- */
extern unsigned char *mock_read_buf;
extern int mock_read_len;
extern int mock_read_pos;

/* -- Binary response protocol constants (mirrors papi.c internals) -------- */
#define RPARAM_HASH              16
#define RPARAM_ARRAY             17
#define RPARAM_SMALL_NUM_BASE    200
#define RPARAM_SHORT_STR_BASE    100
#define RPARAM_END               255

/* Helper: set mock read buffer to a 4-byte size prefix followed by payload.
 * get_result() first reads sizeof(uint32_t) for the size, then the payload. */
static void mock_set_response(const unsigned char *payload, int payload_len) {
  if (mock_read_buf) free(mock_read_buf);
  mock_read_len = 4 + payload_len;
  mock_read_buf = (unsigned char *)malloc(mock_read_len);
  uint32_t size = (uint32_t)payload_len;
  memcpy(mock_read_buf, &size, 4);
  memcpy(mock_read_buf + 4, payload, payload_len);
  mock_read_pos = 0;
}

/* A fake psync_socket -- get_result only passes it through to readall stubs. */
static psync_socket fake_sock;

/* =========================================================================
 *  Part 1: Serialization -- do_prepare_command
 * ========================================================================= */

START_TEST(prepare_command_no_params) {
  size_t retlen;
  unsigned char *data = do_prepare_command("nop", 3, NULL, 0, -1, 0, &retlen);
  ck_assert_ptr_nonnull(data);

  /* Expected: 2-byte plen + 1-byte cmdlen + 3 bytes "nop" + 1-byte paramcnt(0)
   * plen = cmdlen(3) + 2 = 5   ->  wire size = 5 + 2 = 7 */
  ck_assert_uint_eq(retlen, (size_t)7);

  /* Bytes 0-1: plen = 5 (little-endian) */
  uint16_t plen;
  memcpy(&plen, data, 2);
  ck_assert_uint_eq(plen, (uint16_t)5);

  /* Byte 2: cmdlen = 3 (no 0x80 flag since datalen == -1) */
  ck_assert_uint_eq(data[2], (unsigned char)3);

  /* Bytes 3-5: "nop" */
  ck_assert(memcmp(data + 3, "nop", 3) == 0);

  /* Byte 6: paramcnt = 0 */
  ck_assert_uint_eq(data[6], (unsigned char)0);

  free(data);
} END_TEST

START_TEST(prepare_command_str_param) {
  binparam params[] = { P_STR("auth", "tok") };
  size_t retlen;
  unsigned char *data = do_prepare_command("cmd", 3, params, 1, -1, 0, &retlen);
  ck_assert_ptr_nonnull(data);

  /* Skip 2-byte plen + 1-byte cmdlen + 3 "cmd" + 1 paramcnt = offset 7 */
  unsigned char *p = data + 7;

  /* Type/namelen byte: (PARAM_STR << 6) + strlen("auth") = 0 + 4 = 4 */
  ck_assert_uint_eq(*p, (unsigned char)4);
  p++;

  /* Param name: "auth" */
  ck_assert(memcmp(p, "auth", 4) == 0);
  p += 4;

  /* 4-byte string length: strlen("tok") = 3 (LE) */
  uint32_t slen;
  memcpy(&slen, p, 4);
  ck_assert_uint_eq(slen, (uint32_t)3);
  p += 4;

  /* String data: "tok" */
  ck_assert(memcmp(p, "tok", 3) == 0);

  free(data);
} END_TEST

START_TEST(prepare_command_num_param) {
  binparam params[] = { P_NUM("id", 42) };
  size_t retlen;
  unsigned char *data = do_prepare_command("cmd", 3, params, 1, -1, 0, &retlen);
  ck_assert_ptr_nonnull(data);

  unsigned char *p = data + 7; /* skip header */

  /* Type/namelen: (PARAM_NUM << 6) + strlen("id") = 64 + 2 = 66 */
  ck_assert_uint_eq(*p, (unsigned char)66);
  p++;

  /* Name: "id" */
  ck_assert(memcmp(p, "id", 2) == 0);
  p += 2;

  /* 8-byte uint64 value */
  uint64_t val;
  memcpy(&val, p, 8);
  ck_assert_uint_eq(val, (uint64_t)42);

  free(data);
} END_TEST

START_TEST(prepare_command_bool_param) {
  binparam params[] = { P_BOOL("flag", 1) };
  size_t retlen;
  unsigned char *data = do_prepare_command("cmd", 3, params, 1, -1, 0, &retlen);
  ck_assert_ptr_nonnull(data);

  unsigned char *p = data + 7; /* skip header */

  /* Type/namelen: (PARAM_BOOL << 6) + strlen("flag") = 128 + 4 = 132 */
  ck_assert_uint_eq(*p, (unsigned char)132);
  p++;

  /* Name: "flag" */
  ck_assert(memcmp(p, "flag", 4) == 0);
  p += 4;

  /* Bool value: 1 */
  ck_assert_uint_eq(*p, (unsigned char)1);

  free(data);
} END_TEST

START_TEST(prepare_command_mixed_params) {
  binparam params[] = {
    P_STR("auth", "tok"),
    P_NUM("id", 7),
    P_BOOL("ok", 0)
  };
  size_t retlen;
  unsigned char *data = do_prepare_command("mix", 3, params, 3, -1, 0, &retlen);
  ck_assert_ptr_nonnull(data);

  /* Verify plen is self-consistent: plen from header must equal retlen - 2 */
  uint16_t plen;
  memcpy(&plen, data, 2);
  ck_assert_uint_eq((size_t)plen, retlen - 2);

  /* Byte after header: cmdlen = 3 */
  ck_assert_uint_eq(data[2], (unsigned char)3);

  /* Byte 6: paramcnt = 3 */
  ck_assert_uint_eq(data[6], (unsigned char)3);

  /* Verify total size:
   *   header: 2 (plen) + 1 (cmdlen) + 3 (cmd) + 1 (paramcnt) = 7
   *   str "auth"/"tok": 1 + 4 + 4 + 3 = 12
   *   num "id"/7:       1 + 2 + 8     = 11
   *   bool "ok"/0:      1 + 2 + 1     = 4
   *   total = 7 + 12 + 11 + 4 = 34 */
  ck_assert_uint_eq(retlen, (size_t)34);

  free(data);
} END_TEST

START_TEST(prepare_command_with_data) {
  size_t retlen;
  int64_t datalen = 100;
  unsigned char *data = do_prepare_command("up", 2, NULL, 0, datalen, 0, &retlen);
  ck_assert_ptr_nonnull(data);

  /* With datalen >= 0:
   *   plen = cmdlen(2) + 2 + sizeof(uint64_t) = 12
   *   wire = 12 + 2 = 14 */
  ck_assert_uint_eq(retlen, (size_t)14);

  /* cmdlen byte must have 0x80 flag: 2 | 0x80 = 0x82 */
  ck_assert_uint_eq(data[2], (unsigned char)0x82);

  /* 8-byte datalen follows cmdlen byte */
  uint64_t dl;
  memcpy(&dl, data + 3, 8);
  ck_assert_uint_eq(dl, (uint64_t)100);

  /* Command bytes follow datalen */
  ck_assert(memcmp(data + 11, "up", 2) == 0);

  /* paramcnt = 0 */
  ck_assert_uint_eq(data[13], (unsigned char)0);

  free(data);
} END_TEST

/* =========================================================================
 *  Part 2: Deserialization -- get_result, find_result, check_result
 * ========================================================================= */

/* Synthetic response: hash { result: 0, hostname: "test" }
 *
 * Wire bytes (response payload, after the 4-byte size prefix):
 *   RPARAM_HASH
 *     key "result"  -> RPARAM_SHORT_STR_BASE+6, "result"
 *     val 0         -> RPARAM_SMALL_NUM_BASE+0
 *     key "hostname"-> RPARAM_SHORT_STR_BASE+8, "hostname"
 *     val "test"    -> RPARAM_SHORT_STR_BASE+4, "test"
 *   RPARAM_END
 */
static const unsigned char resp_hash_simple[] = {
  RPARAM_HASH,
  RPARAM_SHORT_STR_BASE + 6, 'r','e','s','u','l','t',
  RPARAM_SMALL_NUM_BASE + 0,
  RPARAM_SHORT_STR_BASE + 8, 'h','o','s','t','n','a','m','e',
  RPARAM_SHORT_STR_BASE + 4, 't','e','s','t',
  RPARAM_END
};

START_TEST(get_result_simple_hash) {
  mock_set_response(resp_hash_simple, sizeof(resp_hash_simple));
  binresult *res = get_result(&fake_sock);
  ck_assert_ptr_nonnull(res);
  ck_assert_uint_eq(res->type, (uint32_t)PARAM_HASH);
  ck_assert_uint_eq(res->length, (uint32_t)2);
  free(res);
} END_TEST

START_TEST(find_result_existing_key) {
  mock_set_response(resp_hash_simple, sizeof(resp_hash_simple));
  binresult *res = get_result(&fake_sock);
  ck_assert_ptr_nonnull(res);

  const binresult *val = psync_find_result(res, "result", PARAM_NUM);
  ck_assert_ptr_nonnull(val);
  ck_assert_uint_eq(val->type, (uint32_t)PARAM_NUM);
  ck_assert_uint_eq(val->num, (uint64_t)0);
  free(res);
} END_TEST

START_TEST(find_result_wrong_type) {
  mock_set_response(resp_hash_simple, sizeof(resp_hash_simple));
  binresult *res = get_result(&fake_sock);
  ck_assert_ptr_nonnull(res);

  /* "result" is a number but we ask for string -- check_result returns NULL */
  const binresult *val = psync_check_result(res, "result", PARAM_STR);
  ck_assert_ptr_null(val);
  free(res);
} END_TEST

START_TEST(get_result_with_string) {
  mock_set_response(resp_hash_simple, sizeof(resp_hash_simple));
  binresult *res = get_result(&fake_sock);
  ck_assert_ptr_nonnull(res);

  const binresult *val = psync_find_result(res, "hostname", PARAM_STR);
  ck_assert_ptr_nonnull(val);
  ck_assert_uint_eq(val->type, (uint32_t)PARAM_STR);
  ck_assert_str_eq(val->str, "test");
  ck_assert_uint_eq(val->length, (uint32_t)4);
  free(res);
} END_TEST

/* Synthetic response: hash { result: 0, items: [1, 2] } */
static const unsigned char resp_hash_array[] = {
  RPARAM_HASH,
  RPARAM_SHORT_STR_BASE + 6, 'r','e','s','u','l','t',
  RPARAM_SMALL_NUM_BASE + 0,
  RPARAM_SHORT_STR_BASE + 5, 'i','t','e','m','s',
  RPARAM_ARRAY,
    RPARAM_SMALL_NUM_BASE + 1,
    RPARAM_SMALL_NUM_BASE + 2,
  RPARAM_END,  /* end of array */
  RPARAM_END   /* end of hash */
};

START_TEST(get_result_with_array) {
  mock_set_response(resp_hash_array, sizeof(resp_hash_array));
  binresult *res = get_result(&fake_sock);
  ck_assert_ptr_nonnull(res);

  const binresult *arr = psync_find_result(res, "items", PARAM_ARRAY);
  ck_assert_ptr_nonnull(arr);
  ck_assert_uint_eq(arr->type, (uint32_t)PARAM_ARRAY);
  ck_assert_uint_eq(arr->length, (uint32_t)2);
  ck_assert_ptr_nonnull(arr->array);
  ck_assert_uint_eq(arr->array[0]->type, (uint32_t)PARAM_NUM);
  ck_assert_uint_eq(arr->array[0]->num, (uint64_t)1);
  ck_assert_uint_eq(arr->array[1]->type, (uint32_t)PARAM_NUM);
  ck_assert_uint_eq(arr->array[1]->num, (uint64_t)2);
  free(res);
} END_TEST

/* ========================================================================= */

static void teardown_mock(void) {
  if (mock_read_buf) free(mock_read_buf);
  mock_read_buf = NULL;
}

static Suite *papi_suite(void) {
  Suite *s = suite_create("papi");

  TCase *tc_ser = tcase_create("serialization");
  tcase_add_test(tc_ser, prepare_command_no_params);
  tcase_add_test(tc_ser, prepare_command_str_param);
  tcase_add_test(tc_ser, prepare_command_num_param);
  tcase_add_test(tc_ser, prepare_command_bool_param);
  tcase_add_test(tc_ser, prepare_command_mixed_params);
  tcase_add_test(tc_ser, prepare_command_with_data);
  suite_add_tcase(s, tc_ser);

  TCase *tc_deser = tcase_create("deserialization");
  tcase_add_checked_fixture(tc_deser, NULL, teardown_mock);
  tcase_add_test(tc_deser, get_result_simple_hash);
  tcase_add_test(tc_deser, find_result_existing_key);
  tcase_add_test(tc_deser, find_result_wrong_type);
  tcase_add_test(tc_deser, get_result_with_string);
  tcase_add_test(tc_deser, get_result_with_array);
  suite_add_tcase(s, tc_deser);

  return s;
}

int main(void) {
  Suite *s = papi_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
