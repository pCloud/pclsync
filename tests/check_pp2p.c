/* Unit tests for pp2p.c — P2P networking bug fixes.
 *
 * Build:  make check  (included in CORE_BINS)
 * Run:    ./tests/build/check_pp2p
 *
 * Tests the bug fixes in pp2p.c:
 *   - BUG-1: Missing psync_ssl_rsa_get_public() call in psync_p2p_check_rsa()
 *   - BUG-2: Wrong sendto() return value check
 *   - BUG-3: Memory leak of pubrsa in psync_p2p_tcphandler()
 *   - BUG-4: Closing INVALID_SOCKET in cleanup
 *   - BUG-5: PSYNC_PURE on impure functions (compile-time, no runtime test)
 *   - socket_write_all / socket_read_all helper functions
 *   - Packet processing edge cases
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "fff.h"
DEFINE_FFF_GLOBALS;

/* --- Core types and macros --- */

typedef uint64_t psync_folderid_t;
typedef uint64_t psync_fileid_t;
typedef uint32_t psync_syncid_t;
typedef uint64_t psync_userid_t;
typedef unsigned long psync_uint_t;

static inline void *psync_malloc(size_t s) { return malloc(s); }
static inline void psync_free(void *p) { free(p); }
static inline char *psync_strdup(const char *s) { return strdup(s); }

#define psync_new(type) ((type *)psync_malloc(sizeof(type)))
#define psync_new_cnt(type, cnt) ((type *)psync_malloc(sizeof(type) * (cnt)))
#define NTO_STR(s) #s
#define TO_STR(s) NTO_STR(s)
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))

#define PSYNC_DIRECTORY_SEPARATORC '/'
#define psync_filename_cmp  strcmp

/* likely/unlikely */
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely_log(x) (x)
#define unlikely_log(x) (x)

/* Debug no-ops */
#define D_NONE     0
#define D_BUG      10
#define D_CRITICAL 20
#define D_ERROR    30
#define D_WARNING  40
#define D_NOTICE   50
#define debug(level, ...) do {} while (0)
#define assertw(cond) do {} while (0)

/* PSYNC_THREAD */
#define PSYNC_THREAD __thread

/* Platform */
#define P_OS_POSIX

/* Socket types and macros */
typedef int psync_socket_t;
#define INVALID_SOCKET -1
#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif
#define psync_sock_err() errno
#define P_WOULDBLOCK EWOULDBLOCK
#define P_AGAIN      EAGAIN
#define P_INTR       EINTR
#define psync_close_socket close
#define psync_read_socket read
#define psync_write_socket write

/* File types */
typedef int psync_file_t;
#define INVALID_HANDLE_VALUE -1
#define P_O_RDONLY 0
#define P_O_WRONLY 1
#define P_O_CREAT  0100
#define P_O_TRUNC  01000

/* Settings macros */
#define PSYNC_SETTING_p2psync "p2psync"
#define _PS(x) PSYNC_SETTING_##x

int psync_do_run = 1;
char psync_my_auth[64] = "testauth";
pthread_mutex_t psync_my_auth_mutex = PTHREAD_MUTEX_INITIALIZER;
uint32_t psync_error = 0;

/* --- SSL types (opaque pointers for test) --- */
typedef void *psync_rsa_t;
typedef void *psync_rsa_publickey_t;
typedef void *psync_rsa_privatekey_t;

typedef struct {
  size_t keylen;
  unsigned char key[];
} psync_symmetric_key_struct_t, *psync_symmetric_key_t;

typedef struct {
  size_t datalen;
  unsigned char data[];
} psync_encrypted_data_struct_t, *psync_encrypted_data_t;

typedef psync_encrypted_data_t psync_encrypted_symmetric_key_t;
typedef psync_encrypted_data_t psync_binary_rsa_key_t;
typedef psync_encrypted_data_t psync_rsa_signature_t;

#define PSYNC_INVALID_RSA NULL
#define PSYNC_INVALID_SYM_KEY NULL
#define PSYNC_INVALID_ENC_SYM_KEY NULL
#define PSYNC_INVALID_BIN_RSA NULL
#define PSYNC_INVALID_ENCODER NULL
#define psync_ssl_alloc_binary_rsa psync_ssl_alloc_encrypted_symmetric_key

typedef void *psync_aes256_encoder;
typedef void *psync_aes256_decoder;

/* Crypto types */
#define PSYNC_AES256_BLOCK_SIZE 16
#define PSYNC_AES256_KEY_SIZE 32
#define PSYNC_CRYPTO_AUTH_SIZE (PSYNC_AES256_BLOCK_SIZE*2)
#define PSYNC_CRYPTO_MAX_HASH_TREE_LEVEL 6

typedef unsigned char psync_crypto_sector_auth_t[PSYNC_CRYPTO_AUTH_SIZE];

typedef struct {
  psync_aes256_encoder encoder;
  union {
    long unsigned __aligner;
    unsigned char iv[PSYNC_AES256_BLOCK_SIZE];
  };
} psync_crypto_aes256_key_struct_t, *psync_crypto_aes256_ctr_encoder_decoder_t;

#define PSYNC_CRYPTO_INVALID_ENCODER NULL

/* Hash sizes */
#define PSYNC_SHA1_BLOCK_LEN 64
#define PSYNC_SHA1_DIGEST_LEN 20
#define PSYNC_SHA1_DIGEST_HEXLEN 40
#define PSYNC_HASH_BLOCK_SIZE    PSYNC_SHA1_BLOCK_LEN
#define PSYNC_HASH_DIGEST_LEN    PSYNC_SHA1_DIGEST_LEN
#define PSYNC_HASH_DIGEST_HEXLEN PSYNC_SHA1_DIGEST_HEXLEN

typedef int psync_hash_ctx;
#define psync_hash_init(ctx) do {} while(0)
#define psync_hash_update(ctx, data, len) do {} while(0)
#define psync_hash_final(out, ctx) memset(out, 0, PSYNC_HASH_DIGEST_LEN)

/* P2P settings */
#define PSYNC_P2P_PORT 42420
#define PSYNC_P2P_HEXHASH_BYTES 3
#define PSYNC_P2P_RSA_SIZE 2048
#define PSYNC_P2P_INITIAL_TIMEOUT 600
#define PSYNC_P2P_SLEEP_WAIT_DOWNLOAD 20000
#define PSYNC_CHECKSUM "sha1"

/* Net result codes */
#define PSYNC_NET_OK        0
#define PSYNC_NET_PERMFAIL -1
#define PSYNC_NET_TEMPFAIL -2

/* --- Downloading hashes type --- */
typedef unsigned char psync_hex_hash[PSYNC_HASH_DIGEST_HEXLEN];
typedef struct {
  size_t hashcnt;
  psync_hex_hash hashes[];
} downloading_files_hashes;

/* --- Interface list type --- */
typedef struct {
  struct sockaddr_storage address;
  struct sockaddr_storage broadcast;
  int addrsize;
} psync_interface_t;

typedef struct {
  size_t interfacecnt;
  psync_interface_t interfaces[];
} psync_interface_list_t;

/* --- Compiler attributes --- */
#define PSYNC_NOINLINE   __attribute__((noinline))
#define PSYNC_MALLOC     __attribute__((malloc))
#define PSYNC_PURE
#define PSYNC_CONST
#define PSYNC_COLD       __attribute__((cold))
#define PSYNC_FORMAT(...)
#define PSYNC_NONNULL(...)
#define PSYNC_PACKED_STRUCT struct __attribute__((__packed__))

/* --- binparam/binresult stubs --- */
typedef struct {
  uint32_t type;
  uint32_t length;
  union { uint64_t num; const char *str; };
} binresult;

typedef struct {
  const char *paramname;
  uint16_t paramnamelen;
  uint16_t paramtype;
  uint32_t opts;
  union { uint64_t num; const char *str; struct { size_t length; const char *data; } lstr; };
} binparam;

#define PARAM_STR   0
#define PARAM_NUM   1
#define PARAM_BOOL  2
#define PARAM_ARRAY 3
#define PARAM_HASH  4
#define PARAM_DATA  5

#define P_STR(name, val) {name, sizeof(name)-1, PARAM_STR, 0, {.str=val}}
#define P_NUM(name, val) {name, sizeof(name)-1, PARAM_NUM, 0, {.num=val}}
#define P_LSTR(name, val, len) {name, sizeof(name)-1, PARAM_STR, 0, {.lstr={len, (const char*)val}}}

static inline const binresult *psync_find_result(const binresult *res, const char *name, int type) {
  (void)name; (void)type;
  return res;
}

/* --- psync_socket stub --- */
typedef struct {
  psync_socket_t sock;
  int pending;
  void *ssl;
} psync_socket;

/* --- FFF Fakes for SSL --- */
FAKE_VALUE_FUNC(psync_rsa_t, psync_ssl_gen_rsa, int);
FAKE_VOID_FUNC(psync_ssl_free_rsa, psync_rsa_t);
FAKE_VALUE_FUNC(psync_rsa_publickey_t, psync_ssl_rsa_get_public, psync_rsa_t);
FAKE_VALUE_FUNC(psync_rsa_privatekey_t, psync_ssl_rsa_get_private, psync_rsa_t);
FAKE_VALUE_FUNC(psync_binary_rsa_key_t, psync_ssl_rsa_public_to_binary, psync_rsa_publickey_t);
FAKE_VOID_FUNC(psync_ssl_rsa_free_public, psync_rsa_publickey_t);
FAKE_VOID_FUNC(psync_ssl_rsa_free_private, psync_rsa_privatekey_t);
FAKE_VOID_FUNC(psync_ssl_rsa_free_binary, psync_binary_rsa_key_t);
FAKE_VALUE_FUNC(psync_rsa_publickey_t, psync_ssl_rsa_binary_to_public, psync_binary_rsa_key_t);
FAKE_VALUE_FUNC(psync_encrypted_symmetric_key_t, psync_ssl_rsa_encrypt_symmetric_key, psync_rsa_publickey_t, const psync_symmetric_key_t);
FAKE_VALUE_FUNC(psync_symmetric_key_t, psync_ssl_rsa_decrypt_symmetric_key, psync_rsa_privatekey_t, const psync_encrypted_symmetric_key_t);
FAKE_VOID_FUNC(psync_ssl_free_symmetric_key, psync_symmetric_key_t);
FAKE_VOID_FUNC(psync_ssl_rand_weak, unsigned char *, int);
FAKE_VALUE_FUNC(psync_encrypted_symmetric_key_t, psync_ssl_alloc_encrypted_symmetric_key, size_t);

/* --- FFF Fakes for crypto --- */
FAKE_VALUE_FUNC(psync_symmetric_key_t, psync_crypto_aes256_ctr_gen_key);
FAKE_VALUE_FUNC(psync_crypto_aes256_ctr_encoder_decoder_t, psync_crypto_aes256_ctr_encoder_decoder_create, psync_symmetric_key_t);
FAKE_VOID_FUNC(psync_crypto_aes256_ctr_encoder_decoder_free, psync_crypto_aes256_ctr_encoder_decoder_t);
FAKE_VOID_FUNC(psync_crypto_aes256_ctr_encode_decode_inplace, psync_crypto_aes256_ctr_encoder_decoder_t, void *, size_t, uint64_t);

/* --- FFF Fakes for hash/encoding --- */
FAKE_VOID_FUNC(psync_hash, const void *, size_t, void *);
FAKE_VOID_FUNC(psync_binhex, void *, const void *, size_t);

/* --- SQL stubs --- */
typedef struct { void *stmt; } psync_sql_res;
typedef const uint64_t *psync_uint_row;
typedef struct {
  uint32_t type; uint32_t length;
  union { uint64_t num; int64_t snum; const char *str; double real; };
} psync_variant;
typedef const psync_variant *psync_variant_row;

#define PSYNC_TSTRING 3

static inline uint64_t psync_get_number(psync_variant v) { return v.num; }
static inline const char *psync_get_string(psync_variant v) { return v.str; }

psync_sql_res *psync_sql_query_rdlock(const char *sql) { (void)sql; return NULL; }
void psync_sql_bind_lstring(psync_sql_res *r, int p, const char *v, size_t l) { (void)r; (void)p; (void)v; (void)l; }
void psync_sql_bind_uint(psync_sql_res *r, int p, uint64_t v) { (void)r; (void)p; (void)v; }
void psync_sql_free_result(psync_sql_res *r) { (void)r; }
psync_variant_row psync_sql_fetch_row(psync_sql_res *r) { (void)r; return NULL; }

/* --- Subsystem stubs --- */
FAKE_VALUE_FUNC(downloading_files_hashes *, psync_get_downloading_hashes);
FAKE_VALUE_FUNC(char *, psync_local_path_for_local_file, psync_fileid_t, size_t *);
FAKE_VALUE_FUNC(int, psync_setting_get_bool, const char *);
FAKE_VOID_FUNC(psync_wait_statuses_array, const uint32_t *, int);
FAKE_VALUE_FUNC(psync_interface_list_t *, psync_list_ip_adapters);
FAKE_VOID_FUNC(psync_timer_exception_handler, void *);
FAKE_VOID_FUNC(psync_milisleep, uint64_t);
FAKE_VALUE_FUNC(psync_socket_t, psync_create_socket, int, int, int);
FAKE_VALUE_FUNC(int, psync_select_in, psync_socket_t *, int, int64_t);

FAKE_VALUE_FUNC(psync_socket *, psync_apipool_get);
FAKE_VALUE_FUNC(binresult *, send_command, psync_socket *, const char *, const binparam *);
FAKE_VOID_FUNC(psync_apipool_release, psync_socket *);
FAKE_VOID_FUNC(psync_apipool_release_bad, psync_socket *);

FAKE_VALUE_FUNC(psync_file_t, psync_file_open, const char *, int, int);
FAKE_VALUE_FUNC(ssize_t, psync_file_read, psync_file_t, void *, size_t);
FAKE_VALUE_FUNC(ssize_t, psync_file_write, psync_file_t, const void *, size_t);
FAKE_VALUE_FUNC(int, psync_file_close, psync_file_t);

#define psync_run_thread(name, func) ((void)0)
#define psync_run_thread1(name, func, param) ((void)0)

/* --- Pstatus stubs --- */
#define PSTATUS_TYPE_AUTH   0
#define PSTATUS_TYPE_RUN    1
#define PSTATUS_TYPE_ONLINE 2
#define PSTATUS_AUTH_PROVIDED 1
#define PSTATUS_RUN_RUN 1
#define PSTATUS_ONLINE_ONLINE 1
#define PSTATUS_COMBINE(a, b) ((a) | ((b) << 16))

/* --- Pre-define include guards --- */
#define _PSYNC_COMPAT_H
#define _PSYNC_COMPILER_H
#define _PSYNC_SETTINGS_H
#define _PSYNC_LIBS_H
#define _PSYNC_CORE_H
#define _PSYNC_SQL_H
#define _PSYNC_TIMER_H
#define _PSYNC_STATUS_H
#define _PSYNC_SSL_H
#define _PSYNC_DOWNLOAD_H
#define _PSYNC_NETLIBS_H
#define _PSYNC_API_H
#define _PSYNC_P2P_H
#define _PSYNC_CRYPTO_H
#define _PSYNC_FOLDER_H
#define _PSYNC_CALLBACKS_H

/* Include pp2p.c directly — suppress warnings for unused static functions
 * and variables that are only exercised in the full library build. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "../pp2p.c"
#pragma GCC diagnostic pop

/* ══════════════════════════════════════════════════════════════════════════
 *  Helpers
 * ══════════════════════════════════════════════════════════════════════════ */

static void setup(void) {
  RESET_FAKE(psync_ssl_gen_rsa);
  RESET_FAKE(psync_ssl_free_rsa);
  RESET_FAKE(psync_ssl_rsa_get_public);
  RESET_FAKE(psync_ssl_rsa_get_private);
  RESET_FAKE(psync_ssl_rsa_public_to_binary);
  RESET_FAKE(psync_ssl_rsa_free_public);
  RESET_FAKE(psync_ssl_rsa_free_private);
  RESET_FAKE(psync_ssl_rsa_free_binary);
  RESET_FAKE(psync_ssl_rsa_binary_to_public);
  RESET_FAKE(psync_ssl_rsa_encrypt_symmetric_key);
  RESET_FAKE(psync_ssl_free_symmetric_key);
  RESET_FAKE(psync_ssl_rand_weak);
  RESET_FAKE(psync_ssl_alloc_encrypted_symmetric_key);
  RESET_FAKE(psync_crypto_aes256_ctr_gen_key);
  RESET_FAKE(psync_crypto_aes256_ctr_encoder_decoder_create);
  RESET_FAKE(psync_crypto_aes256_ctr_encoder_decoder_free);
  RESET_FAKE(psync_get_downloading_hashes);
  RESET_FAKE(psync_local_path_for_local_file);
  RESET_FAKE(psync_setting_get_bool);
  RESET_FAKE(psync_apipool_get);
  RESET_FAKE(send_command);
  FFF_RESET_HISTORY();

  /* Reset module globals */
  psync_rsa_public = PSYNC_INVALID_RSA;
  psync_rsa_private = PSYNC_INVALID_RSA;
  psync_rsa_public_bin = PSYNC_INVALID_BIN_RSA;
}

/* Sentinel values for fake RSA objects */
static int fake_rsa_obj;
static int fake_rsa_pub_obj;
static int fake_rsa_priv_obj;
static int fake_rsa_pubbin_obj;

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: psync_p2p_check_rsa (BUG-1 fix)
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_check_rsa_calls_get_public) {
  /* After fix, psync_ssl_rsa_get_public must be called */
  psync_ssl_gen_rsa_fake.return_val = &fake_rsa_obj;
  psync_ssl_rsa_get_private_fake.return_val = &fake_rsa_priv_obj;
  psync_ssl_rsa_get_public_fake.return_val = &fake_rsa_pub_obj;
  psync_ssl_rsa_public_to_binary_fake.return_val = (psync_binary_rsa_key_t)&fake_rsa_pubbin_obj;

  int ret = psync_p2p_check_rsa();
  ck_assert_int_eq(ret, 0);
  ck_assert_uint_eq(psync_ssl_rsa_get_public_fake.call_count, 1);
} END_TEST

START_TEST(test_check_rsa_success_sets_all) {
  psync_ssl_gen_rsa_fake.return_val = &fake_rsa_obj;
  psync_ssl_rsa_get_private_fake.return_val = &fake_rsa_priv_obj;
  psync_ssl_rsa_get_public_fake.return_val = &fake_rsa_pub_obj;
  psync_ssl_rsa_public_to_binary_fake.return_val = (psync_binary_rsa_key_t)&fake_rsa_pubbin_obj;

  int ret = psync_p2p_check_rsa();
  ck_assert_int_eq(ret, 0);
  ck_assert_ptr_eq(psync_rsa_private, &fake_rsa_priv_obj);
  ck_assert_ptr_eq(psync_rsa_public, &fake_rsa_pub_obj);
  ck_assert_ptr_eq(psync_rsa_public_bin, (psync_binary_rsa_key_t)&fake_rsa_pubbin_obj);
} END_TEST

START_TEST(test_check_rsa_gen_failure) {
  psync_ssl_gen_rsa_fake.return_val = PSYNC_INVALID_RSA;

  int ret = psync_p2p_check_rsa();
  ck_assert_int_eq(ret, -1);
  ck_assert_ptr_eq(psync_rsa_private, PSYNC_INVALID_RSA);
} END_TEST

START_TEST(test_check_rsa_get_public_failure) {
  /* get_private succeeds but get_public fails */
  psync_ssl_gen_rsa_fake.return_val = &fake_rsa_obj;
  psync_ssl_rsa_get_private_fake.return_val = &fake_rsa_priv_obj;
  psync_ssl_rsa_get_public_fake.return_val = PSYNC_INVALID_RSA;

  int ret = psync_p2p_check_rsa();
  ck_assert_int_eq(ret, -1);
  /* private key should be freed on partial failure */
  ck_assert_uint_eq(psync_ssl_rsa_free_private_fake.call_count, 1);
} END_TEST

START_TEST(test_check_rsa_idempotent) {
  /* Second call should not regenerate if already set */
  psync_ssl_gen_rsa_fake.return_val = &fake_rsa_obj;
  psync_ssl_rsa_get_private_fake.return_val = &fake_rsa_priv_obj;
  psync_ssl_rsa_get_public_fake.return_val = &fake_rsa_pub_obj;
  psync_ssl_rsa_public_to_binary_fake.return_val = (psync_binary_rsa_key_t)&fake_rsa_pubbin_obj;

  ck_assert_int_eq(psync_p2p_check_rsa(), 0);
  ck_assert_int_eq(psync_p2p_check_rsa(), 0);
  /* gen_rsa should only be called once */
  ck_assert_uint_eq(psync_ssl_gen_rsa_fake.call_count, 1);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: socket_write_all / socket_read_all helpers
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_socket_write_read_roundtrip) {
  int pipefd[2];
  ck_assert_int_eq(pipe(pipefd), 0);

  unsigned char data[] = "hello p2p world";
  unsigned char buf[sizeof(data)];

  ck_assert_int_eq(socket_write_all(pipefd[1], data, sizeof(data)), 0);
  ck_assert_int_eq(socket_read_all(pipefd[0], buf, sizeof(data)), 0);
  ck_assert_mem_eq(buf, data, sizeof(data));

  close(pipefd[0]);
  close(pipefd[1]);
} END_TEST

START_TEST(test_socket_read_all_eof) {
  int pipefd[2];
  ck_assert_int_eq(pipe(pipefd), 0);

  /* Write 5 bytes then close writer — reader wants 10 */
  unsigned char data[5] = {1, 2, 3, 4, 5};
  write(pipefd[1], data, sizeof(data));
  close(pipefd[1]);

  unsigned char buf[10];
  ck_assert_int_eq(socket_read_all(pipefd[0], buf, 10), -1);

  close(pipefd[0]);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: psync_p2p_process_packet edge cases
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_process_packet_too_small) {
  /* Packet smaller than sizeof(packet_type_t) should be silently dropped */
  char tiny[2] = {0, 0};
  psync_p2p_process_packet(tiny, 2);
  /* No crash — success */
} END_TEST

START_TEST(test_process_packet_invalid_type) {
  /* Packet with type >= ARRAY_SIZE(min_packet_size) should be dropped */
  uint32_t type = 99;
  psync_p2p_process_packet((const char *)&type, sizeof(type));
  /* No crash — success */
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: psync_p2p_has_file (SQL returns no rows)
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_has_file_no_match) {
  unsigned char hashstart[PSYNC_P2P_HEXHASH_BYTES] = {0};
  unsigned char genhash[PSYNC_HASH_DIGEST_HEXLEN] = {0};
  unsigned char rand[PSYNC_HASH_BLOCK_SIZE - PSYNC_HASH_DIGEST_HEXLEN] = {0};
  unsigned char realhash[PSYNC_HASH_DIGEST_HEXLEN];

  /* SQL stubs return NULL rows, so no match */
  psync_fileid_t ret = psync_p2p_has_file(hashstart, genhash, rand, 1000, realhash);
  ck_assert_uint_eq(ret, 0);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: psync_p2p_is_downloading (no hashes match)
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_is_downloading_no_match) {
  /* Create empty hash list */
  downloading_files_hashes *hashes = malloc(sizeof(downloading_files_hashes));
  hashes->hashcnt = 0;
  psync_get_downloading_hashes_fake.return_val = hashes;

  unsigned char hashstart[PSYNC_P2P_HEXHASH_BYTES] = {0};
  unsigned char genhash[PSYNC_HASH_DIGEST_HEXLEN] = {0};
  unsigned char rand[PSYNC_HASH_BLOCK_SIZE - PSYNC_HASH_DIGEST_HEXLEN] = {0};
  unsigned char realhash[PSYNC_HASH_DIGEST_HEXLEN];

  int ret = psync_p2p_is_downloading(hashstart, genhash, rand, 1000, realhash);
  ck_assert_int_eq(ret, 0);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Suite setup
 * ══════════════════════════════════════════════════════════════════════════ */

static Suite *pp2p_suite(void) {
  Suite *s = suite_create("pp2p");

  TCase *tc_rsa = tcase_create("check_rsa");
  tcase_add_checked_fixture(tc_rsa, setup, NULL);
  tcase_add_test(tc_rsa, test_check_rsa_calls_get_public);
  tcase_add_test(tc_rsa, test_check_rsa_success_sets_all);
  tcase_add_test(tc_rsa, test_check_rsa_gen_failure);
  tcase_add_test(tc_rsa, test_check_rsa_get_public_failure);
  tcase_add_test(tc_rsa, test_check_rsa_idempotent);
  suite_add_tcase(s, tc_rsa);

  TCase *tc_sock = tcase_create("socket_helpers");
  tcase_add_test(tc_sock, test_socket_write_read_roundtrip);
  tcase_add_test(tc_sock, test_socket_read_all_eof);
  suite_add_tcase(s, tc_sock);

  TCase *tc_pkt = tcase_create("packet_processing");
  tcase_add_checked_fixture(tc_pkt, setup, NULL);
  tcase_add_test(tc_pkt, test_process_packet_too_small);
  tcase_add_test(tc_pkt, test_process_packet_invalid_type);
  tcase_add_test(tc_pkt, test_has_file_no_match);
  tcase_add_test(tc_pkt, test_is_downloading_no_match);
  suite_add_tcase(s, tc_pkt);

  return s;
}

int main(void) {
  Suite *s = pp2p_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
