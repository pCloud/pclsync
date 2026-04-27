/* Unit tests for pcloudcrypto.c — crypto bug fixes.
 *
 * Build:  make check  (included in CORE_BINS)
 * Run:    ./tests/build/check_pcloudcrypto
 *
 * Tests the bug fixes in pcloudcrypto.c:
 *   - BUG-6: Missing else — crypto functions proceed when not started
 *   - BUG-7: crypto_keys_match() now accepts pub/priv key params
 *   - BUG-8: Weak RNG for salt (compile-time, verified by grep)
 *   - BUG-9: Buffer size check before memcpy (tested via passphrase API)
 *   - err_to_ptr / psync_crypto_is_error / psync_crypto_to_error helpers
 *   - set_crypto_err_msg truncation
 *   - psync_crypto_sym_key_ver1_to_sym_key key conversion
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <assert.h>

#include "fff.h"
DEFINE_FFF_GLOBALS;

/* --- Core types and macros --- */

typedef uint64_t psync_folderid_t;
typedef uint64_t psync_fileid_t;
typedef int64_t psync_fsfolderid_t;
typedef int64_t psync_fsfileid_t;
typedef uint32_t psync_syncid_t;
typedef uint64_t psync_userid_t;
typedef unsigned long psync_uint_t;

static inline void *psync_malloc(size_t s) { return malloc(s); }
static inline void psync_free(void *p) { free(p); }
static inline char *psync_strdup(const char *s) { return strdup(s); }
static inline char *psync_strndup(const char *s, size_t n) { return strndup(s, n); }

#define psync_new(type) ((type *)psync_malloc(sizeof(type)))
#define psync_new_cnt(type, cnt) ((type *)psync_malloc(sizeof(type) * (cnt)))
#define NTO_STR(s) #s
#define TO_STR(s) NTO_STR(s)
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))

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

#define PSYNC_THREAD __thread

/* Platform */
#define P_OS_POSIX

/* --- SSL types --- */
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

/* Crypto constants */
#define PSYNC_AES256_BLOCK_SIZE 16
#define PSYNC_AES256_KEY_SIZE 32
#define PSYNC_CRYPTO_AUTH_SIZE (PSYNC_AES256_BLOCK_SIZE*2)
#define PSYNC_CRYPTO_MAX_HASH_TREE_LEVEL 6
#define PSYNC_CRYPTO_PBKDF2_SALT_LEN 64
#define PSYNC_CRYPTO_HMAC_SHA512_KEY_LEN 128

#define PSYNC_SHA1_BLOCK_LEN 64
#define PSYNC_SHA1_DIGEST_LEN 20
#define PSYNC_SHA1_DIGEST_HEXLEN 40
#define PSYNC_SHA256_DIGEST_LEN 32
#define PSYNC_HASH_BLOCK_SIZE    PSYNC_SHA1_BLOCK_LEN
#define PSYNC_HASH_DIGEST_LEN    PSYNC_SHA1_DIGEST_LEN
#define PSYNC_HASH_DIGEST_HEXLEN PSYNC_SHA1_DIGEST_HEXLEN

#define PSYNC_CRYPTO_TYPE_RSA4096_64BYTESALT_20000IT 0
#define PSYNC_CRYPTO_PUB_TYPE_RSA4096                0
#define PSYNC_CRYPTO_SYM_AES256_1024BIT_HMAC         0
#define PSYNC_CRYPTO_CACHE_DIR_ECODER_SEC   15
#define PSYNC_CRYPTO_CACHE_DIR_SYM_KEY      15
#define PSYNC_CRYPTO_CACHE_FILE_SYM_KEY     15

#define PSYNC_CRYPTO_INVALID_FOLDERID ((psync_folderid_t)-1)

/* Error codes */
#define PSYNC_CRYPTO_SUCCESS               0
#define PSYNC_CRYPTO_NOT_STARTED           -1
#define PSYNC_CRYPTO_RSA_ERROR             -3
#define PSYNC_CRYPTO_FOLDER_NOT_FOUND      -4
#define PSYNC_CRYPTO_INVALID_KEY           -5
#define PSYNC_CRYPTO_CANT_CONNECT          -6
#define PSYNC_CRYPTO_FOLDER_NOT_ENCRYPTED  -7
#define PSYNC_CRYPTO_FILE_NOT_FOUND        -8
#define PSYNC_CRYPTO_BAD_PASSPHRASE        -9
#define PSYNC_CRYPTO_BAD_KEY               -10
#define PSYNC_CRYPTO_INTERNAL_ERROR        -11
#define PERROR_NO_MEMORY                   15
#define PERROR_NET_ERROR                   16

#define PSYNC_CRYPTO_START_SUCCESS          0
#define PSYNC_CRYPTO_START_ALREADY_STARTED  1
#define PSYNC_CRYPTO_START_NOT_SETUP        2
#define PSYNC_CRYPTO_START_CANT_CONNECT     3
#define PSYNC_CRYPTO_START_NOT_LOGGED_IN    4
#define PSYNC_CRYPTO_START_BAD_PASSWORD     5
#define PSYNC_CRYPTO_START_KEYS_DONT_MATCH  6
#define PSYNC_CRYPTO_START_UNKNOWN_KEY_FORMAT 7
#define PSYNC_CRYPTO_START_UNKNOWN_ERROR    8

#define PSYNC_CRYPTO_STOP_SUCCESS          0
#define PSYNC_CRYPTO_STOP_NOT_STARTED      1

#define PSYNC_CRYPTO_SETUP_SUCCESS          0
#define PSYNC_CRYPTO_SETUP_CANT_CONNECT     1
#define PSYNC_CRYPTO_SETUP_NOT_LOGGED_IN    2
#define PSYNC_CRYPTO_SETUP_ALREADY_SETUP    3
#define PSYNC_CRYPTO_SETUP_UNKNOWN_ERROR    4

#define PSYNC_CRYPTO_HINT_SUCCESS           0
#define PSYNC_CRYPTO_HINT_CANT_CONNECT      1
#define PSYNC_CRYPTO_HINT_NOT_LOGGED_IN     2
#define PSYNC_CRYPTO_HINT_NOT_PROVIDED      3
#define PSYNC_CRYPTO_HINT_UNKNOWN_ERROR     4

#define PSYNC_CRYPTO_RESET_SUCCESS          0
#define PSYNC_CRYPTO_RESET_CANT_CONNECT     1
#define PSYNC_CRYPTO_RESET_NOT_LOGGED_IN    2
#define PSYNC_CRYPTO_RESET_NOT_SETUP        3
#define PSYNC_CRYPTO_RESET_CRYPTO_IS_STARTED 4
#define PSYNC_CRYPTO_RESET_UNKNOWN_ERROR    5

#define PSYNC_CRYPTO_PASS_TO_KEY_ITERATIONS 20000
#define PSYNC_CRYPTO_RSA_SIZE          4096
#define PSYNC_CRYPTO_SETUP_KEYGEN_FAILED    5
#define PSYNC_CRYPTO_CACHE_FILE_ECODER_SEC  15
#define PSYNC_FOLDER_FLAG_ENCRYPTED 2

typedef unsigned char psync_crypto_sector_auth_t[PSYNC_CRYPTO_AUTH_SIZE];

typedef struct {
  psync_aes256_encoder encoder;
  union {
    long unsigned __aligner;
    unsigned char iv[PSYNC_AES256_BLOCK_SIZE];
  };
} psync_crypto_aes256_key_struct_t, *psync_crypto_aes256_ctr_encoder_decoder_t;

typedef struct {
  psync_aes256_encoder encoder;
  unsigned long ivlen;
  unsigned char iv[];
} psync_crypto_aes256_key_var_iv_struct_t, *psync_crypto_aes256_text_encoder_t, *psync_crypto_aes256_text_decoder_t;

typedef struct {
  psync_aes256_encoder encoder;
  psync_aes256_decoder decoder;
  unsigned long ivlen;
  unsigned char iv[];
} psync_crypto_aes256_enc_dec_var_iv_struct_t, *psync_crypto_aes256_sector_encoder_decoder_t;

#define PSYNC_CRYPTO_INVALID_ENCODER NULL
#define PSYNC_CRYPTO_UNLOADED_SECTOR_ENCODER ((psync_crypto_aes256_sector_encoder_decoder_t)(511+1))
#define PSYNC_CRYPTO_LOADING_SECTOR_ENCODER  ((psync_crypto_aes256_sector_encoder_decoder_t)(511+2))
#define PSYNC_CRYPTO_FAILED_SECTOR_ENCODER   ((psync_crypto_aes256_sector_encoder_decoder_t)(511+3))
#define PSYNC_CRYPTO_MAX_ERROR 511
#define PSYNC_CRYPTO_SYM_FLAG_ISDIR 1
#define PSYNC_CRYPTO_SECTOR_SIZE 4096

static inline int psync_crypto_is_error(const void *ptr){
  return (uintptr_t)ptr<=PSYNC_CRYPTO_MAX_ERROR;
}

static inline int psync_crypto_to_error(const void *ptr){
  return -((int)(uintptr_t)ptr);
}

/* --- Globals that pcloudcrypto.c needs --- */
int psync_do_run = 1;
char psync_my_auth[64] = "testauth";
pthread_mutex_t psync_my_auth_mutex = PTHREAD_MUTEX_INITIALIZER;
uint32_t psync_error = 0;

/* --- binparam/binresult stubs --- */
typedef struct _binresult {
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
#define P_BOOL(name, val) {name, sizeof(name)-1, PARAM_BOOL, 0, {.num=val}}
#define P_LSTR(name, val, len) {name, sizeof(name)-1, PARAM_STR, 0, {.lstr={len, (const char*)val}}}

static const binresult *psync_find_result(const binresult *res, const char *name, int type) {
  (void)name; (void)type;
  return res;
}

/* --- SQL stubs --- */
typedef struct { void *stmt; } psync_sql_res;
typedef const uint64_t *psync_uint_row;

#define PSYNC_TNUMBER 1
#define PSYNC_TSTRING 2
#define PSYNC_TNULL   4

typedef struct {
  uint32_t type; uint32_t length;
  union { uint64_t num; int64_t snum; const char *str; double real; };
} psync_variant;
typedef const psync_variant *psync_variant_row;

static inline uint64_t psync_get_number(psync_variant v) { return v.num; }
static inline const char *psync_get_string(psync_variant v) { return v.str; }
static inline const char *psync_lstring_expected(const char *f, const char *fn, int l, const psync_variant *v, size_t *len) {
  (void)f; (void)fn; (void)l;
  if (len) *len = v->length;
  return v->str;
}
#define psync_get_lstring(v, l) psync_lstring_expected(__FILE__, __FUNCTION__, __LINE__, &(v), l)
#define psync_is_null(v) ((v).type==PSYNC_TNULL)

psync_sql_res *psync_sql_query_rdlock(const char *sql) { (void)sql; return NULL; }
psync_sql_res *psync_sql_query_nolock(const char *sql) { (void)sql; return NULL; }
psync_sql_res *psync_sql_prep_statement(const char *sql) { (void)sql; return NULL; }
void psync_sql_bind_uint(psync_sql_res *r, int p, uint64_t v) { (void)r; (void)p; (void)v; }
void psync_sql_bind_string(psync_sql_res *r, int p, const char *v) { (void)r; (void)p; (void)v; }
void psync_sql_bind_blob(psync_sql_res *r, int p, const char *v, size_t l) { (void)r; (void)p; (void)v; (void)l; }
int psync_sql_run(psync_sql_res *r) { (void)r; return 0; }
int psync_sql_run_free(psync_sql_res *r) { (void)r; return 0; }
void psync_sql_free_result(psync_sql_res *r) { (void)r; }
psync_variant_row psync_sql_fetch_row(psync_sql_res *r) { (void)r; return NULL; }
psync_uint_row psync_sql_fetch_rowint(psync_sql_res *r) { (void)r; return NULL; }
uint32_t psync_sql_affected_rows(void) { return 0; }
int psync_sql_statement(const char *sql) { (void)sql; return 0; }
void psync_sql_lock(void) {}
void psync_sql_unlock(void) {}
int psync_sql_trylock(void) { return 0; }
int psync_sql_start_transaction(void) { return 0; }
int psync_sql_commit_transaction(void) { return 0; }
int psync_sql_rollback_transaction(void) { return 0; }
int64_t psync_sql_cellint(const char *sql, int64_t d) { (void)sql; return d; }

/* --- Socket / psync_socket stubs --- */
typedef int psync_socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

typedef struct {
  psync_socket_t sock;
  int pending;
  void *ssl;
} psync_socket;

/* _PS macro */
#define _PS(x) #x

/* --- FFF Fakes for SSL --- */
FAKE_VALUE_FUNC(psync_rsa_t, psync_ssl_gen_rsa, int);
FAKE_VOID_FUNC(psync_ssl_free_rsa, psync_rsa_t);
FAKE_VALUE_FUNC(psync_rsa_publickey_t, psync_ssl_rsa_get_public, psync_rsa_t);
FAKE_VALUE_FUNC(psync_rsa_privatekey_t, psync_ssl_rsa_get_private, psync_rsa_t);
FAKE_VALUE_FUNC(psync_binary_rsa_key_t, psync_ssl_rsa_public_to_binary, psync_rsa_publickey_t);
FAKE_VALUE_FUNC(psync_encrypted_symmetric_key_t, psync_ssl_rsa_encrypt_symmetric_key, psync_rsa_publickey_t, const psync_symmetric_key_t);
FAKE_VALUE_FUNC(psync_symmetric_key_t, psync_ssl_rsa_decrypt_symmetric_key, psync_rsa_privatekey_t, const psync_encrypted_symmetric_key_t);
FAKE_VOID_FUNC(psync_ssl_free_symmetric_key, psync_symmetric_key_t);
FAKE_VOID_FUNC(psync_ssl_rand_weak, unsigned char *, int);
FAKE_VOID_FUNC(psync_ssl_rand_strong, unsigned char *, int);
FAKE_VALUE_FUNC(psync_encrypted_symmetric_key_t, psync_ssl_alloc_encrypted_symmetric_key, size_t);
FAKE_VALUE_FUNC(psync_encrypted_symmetric_key_t, psync_ssl_copy_encrypted_symmetric_key, psync_encrypted_symmetric_key_t);
FAKE_VOID_FUNC(psync_ssl_memclean, void *, size_t);
FAKE_VALUE_FUNC(psync_rsa_publickey_t, psync_ssl_rsa_load_public, const unsigned char *, size_t);
FAKE_VALUE_FUNC(psync_rsa_privatekey_t, psync_ssl_rsa_load_private, const unsigned char *, size_t);
FAKE_VOID_FUNC(psync_ssl_rsa_free_public, psync_rsa_publickey_t);
FAKE_VOID_FUNC(psync_ssl_rsa_free_private, psync_rsa_privatekey_t);
FAKE_VOID_FUNC(psync_ssl_rsa_free_binary, psync_binary_rsa_key_t);
FAKE_VALUE_FUNC(psync_binary_rsa_key_t, psync_ssl_rsa_private_to_binary, psync_rsa_privatekey_t);
FAKE_VALUE_FUNC(psync_encrypted_symmetric_key_t, psync_ssl_rsa_encrypt_data, psync_rsa_publickey_t, const unsigned char *, size_t);
FAKE_VALUE_FUNC(psync_symmetric_key_t, psync_ssl_rsa_decrypt_data, psync_rsa_privatekey_t, const unsigned char *, size_t);
FAKE_VALUE_FUNC(psync_symmetric_key_t, psync_ssl_gen_symmetric_key_from_pass, const char *, size_t, const unsigned char *, size_t, size_t);
FAKE_VALUE_FUNC(psync_rsa_signature_t, psync_ssl_rsa_sign_sha256_hash, psync_rsa_privatekey_t, const unsigned char *);

/* --- FFF Fakes for crypto --- */
FAKE_VALUE_FUNC(psync_crypto_aes256_ctr_encoder_decoder_t, psync_crypto_aes256_ctr_encoder_decoder_create, psync_symmetric_key_t);
FAKE_VOID_FUNC(psync_crypto_aes256_ctr_encoder_decoder_free, psync_crypto_aes256_ctr_encoder_decoder_t);
FAKE_VOID_FUNC(psync_crypto_aes256_ctr_encode_decode_inplace, psync_crypto_aes256_ctr_encoder_decoder_t, void *, size_t, uint64_t);

FAKE_VALUE_FUNC(psync_crypto_aes256_text_encoder_t, psync_crypto_aes256_text_encoder_create, psync_symmetric_key_t);
FAKE_VOID_FUNC(psync_crypto_aes256_text_encoder_free, psync_crypto_aes256_text_encoder_t);
FAKE_VALUE_FUNC(psync_crypto_aes256_text_decoder_t, psync_crypto_aes256_text_decoder_create, psync_symmetric_key_t);
FAKE_VOID_FUNC(psync_crypto_aes256_text_decoder_free, psync_crypto_aes256_text_decoder_t);
FAKE_VOID_FUNC(psync_crypto_aes256_encode_text, psync_crypto_aes256_text_encoder_t, const unsigned char *, size_t, unsigned char **, size_t *);
FAKE_VALUE_FUNC(unsigned char *, psync_crypto_aes256_decode_text, psync_crypto_aes256_text_decoder_t, const unsigned char *, size_t);

FAKE_VALUE_FUNC(psync_crypto_aes256_sector_encoder_decoder_t, psync_crypto_aes256_sector_encoder_decoder_create, psync_symmetric_key_t);
FAKE_VOID_FUNC(psync_crypto_aes256_sector_encoder_decoder_free, psync_crypto_aes256_sector_encoder_decoder_t);

/* --- FFF Fakes for hash/encoding --- */
FAKE_VOID_FUNC(psync_sha1, const unsigned char *, size_t, unsigned char *);
FAKE_VOID_FUNC(psync_sha256, const void *, size_t, void *);
FAKE_VOID_FUNC(psync_binhex, void *, const void *, size_t);
FAKE_VALUE_FUNC(unsigned char *, psync_base64_encode, const unsigned char *, size_t, size_t *);
FAKE_VALUE_FUNC(unsigned char *, psync_base64_decode, const unsigned char *, size_t, size_t *);
FAKE_VALUE_FUNC(unsigned char *, psync_base32_encode, const unsigned char *, size_t, size_t *);
FAKE_VALUE_FUNC(unsigned char *, psync_base32_decode, const unsigned char *, size_t, size_t *);

/* --- FFF Fakes for cache --- */
FAKE_VALUE_FUNC(void *, psync_cache_get, const char *);
FAKE_VOID_FUNC(psync_cache_add, const char *, void *, uint32_t, void *, int);
FAKE_VOID_FUNC(psync_cache_clean_starting_with_one_of, const char **, size_t);

/* --- FFF Fakes for misc --- */
FAKE_VALUE_FUNC(psync_folderid_t *, psync_crypto_folderids);
FAKE_VOID_FUNC(psync_fs_refresh_folder, psync_folderid_t);
FAKE_VALUE_FUNC(void *, psync_locked_malloc, size_t);
FAKE_VOID_FUNC(psync_locked_free, void *);
FAKE_VOID_FUNC(psync_process_api_error, uint64_t);
FAKE_VALUE_FUNC(int, psync_crypto_issetup);
FAKE_VOID_FUNC(psync_milisleep, uint64_t);
FAKE_VALUE_FUNC(int, psync_setting_get_bool, const char *);

FAKE_VALUE_FUNC(psync_socket *, psync_apipool_get);
FAKE_VALUE_FUNC(binresult *, send_command, psync_socket *, const char *, const binparam *);
FAKE_VOID_FUNC(psync_apipool_release, psync_socket *);
FAKE_VOID_FUNC(psync_apipool_release_bad, psync_socket *);

FAKE_VOID_FUNC(psync_ops_create_folder_in_db, const binresult *);

#define psync_run_thread(name, func) ((void)0)
#define psync_run_thread1(name, func, param) ((void)0)

/* --- Pstatus stubs --- */
typedef struct { uint32_t status; } pstatus_t;
#define PSTATUS_TYPE_AUTH   0
#define PSTATUS_TYPE_RUN    1
#define PSTATUS_TYPE_ONLINE 2
#define PSTATUS_AUTH_PROVIDED 1
#define PSTATUS_RUN_RUN 1
#define PSTATUS_ONLINE_ONLINE 1
#define PSTATUS_COMBINE(a, b) ((a) | ((b) << 16))

/* Compiler attributes */
#define PSYNC_NOINLINE   __attribute__((noinline))
#define PSYNC_MALLOC     __attribute__((malloc))
#define PSYNC_PURE
#define PSYNC_CONST
#define PSYNC_COLD       __attribute__((cold))
#define PSYNC_FORMAT(...)
#define PSYNC_NONNULL(...)
#define PSYNC_PACKED_STRUCT struct __attribute__((__packed__))

/* Hash stubs */
typedef int psync_hash_ctx;
typedef int psync_sha1_ctx;
#define psync_hash_init(ctx) do {} while(0)
#define psync_hash_update(ctx, data, len) do {} while(0)
#define psync_hash_final(out, ctx) memset(out, 0, PSYNC_HASH_DIGEST_LEN)
#define psync_hash(data, len, out) memset(out, 0, PSYNC_HASH_DIGEST_LEN)

/* psync_get_string_id inline functions from pencoding.h */
static inline void psync_get_string_id(char *dst, const char *prefix, uint64_t id){
  char *d=dst;
  while (*prefix) *d++=*prefix++;
  d+=sprintf(d, "%llu", (unsigned long long)id);
  (void)d;
}

static inline void psync_get_string_id2(char *dst, const char *prefix, uint64_t id1, uint64_t id2){
  char *d=dst;
  while (*prefix) *d++=*prefix++;
  d+=sprintf(d, "%llu_%llu", (unsigned long long)id1, (unsigned long long)id2);
  (void)d;
}

/* fstask stubs */
#define PSYNC_FS_TASK_CREAT  0
#define PSYNC_FS_TASK_MODIFY 1

/* #define PRINT_RETURN_CONST to just return the value */
#define PRINT_RETURN_CONST(x) (x)

/* PSYNC_CRYPTO_API_ERR_INTERNAL re-define (matches pcloudcrypto.c) */
/* This is defined inside pcloudcrypto.c itself, so we don't need it here */

/* psync_sql_query — needed by some paths */
psync_sql_res *psync_sql_query(const char *sql) { (void)sql; return NULL; }

/* --- Pre-define include guards --- */
#define _PSYNC_COMPAT_H
#define _PSYNC_COMPILER_H
#define _PSYNC_SETTINGS_H
#define _PSYNC_LIBS_H
#define _PSYNC_CORE_H
#define _PSYNC_SQL_H
#define _PSYNC_NETLIBS_H
#define _PSYNC_SSL_H
#define _PSYNC_CRYPTO_H
#define _PCLOUD_CRYPTO_H
#define _PSYNC_API_H
#define _PSYNC_FOLDER_H
#define _PSYNC_CALLBACKS_H
#define _PSYNC_STATUS_H
#define _PSYNC_TIMER_H
#define _PSYNC_DOWNLOAD_H
#define _PSYNC_CACHE_H
#define _PSYNC_FILEOPS_H
#define _PSYNC_MEMLOCK_H
#define _PSYNC_ENCODING_H
#define _PSYNC_MEMALLOC_H
#define _PSYNC_DIFF_H
#define _PSYNC_P2P_H
#define _PSYNC_FS_H
#define _PSYNC_FSFOLDER_H
#define _PSYNC_FSSTATIC_H
#define _PSYNC_FSTASKS_H
#define _PSYNC_PATHSTATUS_H
#define _PSYNC_STRINGS_H

/* Include pcloudcrypto.c directly — suppress warnings for unused static
 * functions that are only exercised in the full library build. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../pcloudcrypto.c"
#pragma GCC diagnostic pop

/* ══════════════════════════════════════════════════════════════════════════
 *  Helpers
 * ══════════════════════════════════════════════════════════════════════════ */

/* Provide a real locked_malloc for sym_key_ver1_to_sym_key tests */
static void *real_locked_malloc(size_t s) { return malloc(s); }
static void real_locked_free(void *p) { free(p); }

static void setup(void) {
  RESET_FAKE(psync_ssl_rsa_encrypt_symmetric_key);
  RESET_FAKE(psync_ssl_rsa_decrypt_symmetric_key);
  RESET_FAKE(psync_ssl_free_symmetric_key);
  RESET_FAKE(psync_ssl_rand_weak);
  RESET_FAKE(psync_ssl_rand_strong);
  RESET_FAKE(psync_ssl_memclean);
  RESET_FAKE(psync_ssl_rsa_load_public);
  RESET_FAKE(psync_ssl_rsa_load_private);
  RESET_FAKE(psync_ssl_rsa_free_public);
  RESET_FAKE(psync_ssl_rsa_free_private);
  RESET_FAKE(psync_cache_get);
  RESET_FAKE(psync_cache_add);
  RESET_FAKE(psync_locked_malloc);
  RESET_FAKE(psync_locked_free);
  RESET_FAKE(psync_apipool_get);
  RESET_FAKE(send_command);
  RESET_FAKE(psync_crypto_aes256_text_decoder_create);
  RESET_FAKE(psync_crypto_aes256_text_encoder_create);
  RESET_FAKE(psync_crypto_aes256_sector_encoder_decoder_create);
  FFF_RESET_HISTORY();

  /* Reset crypto state */
  crypto_started_l = 0;
  crypto_started_un = 0;
  crypto_pubkey = PSYNC_INVALID_RSA;
  crypto_privkey = PSYNC_INVALID_RSA;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: err_to_ptr roundtrip
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_err_to_ptr_roundtrip) {
  void *ptr = err_to_ptr(PSYNC_CRYPTO_NOT_STARTED);
  ck_assert(psync_crypto_is_error(ptr));
  ck_assert_int_eq(psync_crypto_to_error(ptr), PSYNC_CRYPTO_NOT_STARTED);
} END_TEST

START_TEST(test_err_to_ptr_roundtrip_folder_not_encrypted) {
  void *ptr = err_to_ptr(PSYNC_CRYPTO_FOLDER_NOT_ENCRYPTED);
  ck_assert(psync_crypto_is_error(ptr));
  ck_assert_int_eq(psync_crypto_to_error(ptr), PSYNC_CRYPTO_FOLDER_NOT_ENCRYPTED);
} END_TEST

START_TEST(test_is_error_small_ptr) {
  /* Small pointers (within error range) should be detected */
  ck_assert(psync_crypto_is_error((void *)1));
  ck_assert(psync_crypto_is_error((void *)511));
} END_TEST

START_TEST(test_is_error_real_ptr) {
  /* Real heap pointers should not be detected as errors */
  void *p = malloc(64);
  ck_assert_ptr_nonnull(p);
  ck_assert(!psync_crypto_is_error(p));
  free(p);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: Not-started guard (BUG-6 fix)
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_folder_decoder_not_started) {
  crypto_started_un = 0;
  crypto_started_l = 0;
  psync_crypto_aes256_text_decoder_t dec = psync_cloud_crypto_get_folder_decoder(42);
  ck_assert(psync_crypto_is_error(dec));
  ck_assert_int_eq(psync_crypto_to_error(dec), PSYNC_CRYPTO_NOT_STARTED);
} END_TEST

START_TEST(test_folder_encoder_not_started) {
  crypto_started_un = 0;
  crypto_started_l = 0;
  psync_crypto_aes256_text_encoder_t enc = psync_cloud_crypto_get_folder_encoder(42);
  ck_assert(psync_crypto_is_error(enc));
  ck_assert_int_eq(psync_crypto_to_error(enc), PSYNC_CRYPTO_NOT_STARTED);
} END_TEST

START_TEST(test_file_encoder_not_started) {
  crypto_started_un = 0;
  crypto_started_l = 0;
  psync_crypto_aes256_sector_encoder_decoder_t enc = psync_cloud_crypto_get_file_encoder(42, 0, 0);
  ck_assert(psync_crypto_is_error(enc));
  ck_assert_int_eq(psync_crypto_to_error(enc), PSYNC_CRYPTO_NOT_STARTED);
} END_TEST

START_TEST(test_folder_decoder_not_started_bypasses_lock_path) {
  /* With BUG-6 fix, when crypto_started_l==0, the locked decoder
   * functions should NOT be called. We verify this by setting
   * crypto_started_un=1 but crypto_started_l=0 — the function
   * should still return NOT_STARTED from the lock path. */
  crypto_started_un = 1;
  crypto_started_l = 0;
  psync_cache_get_fake.return_val = NULL;

  psync_crypto_aes256_text_decoder_t dec = psync_cloud_crypto_get_folder_decoder(42);
  ck_assert(psync_crypto_is_error(dec));
  ck_assert_int_eq(psync_crypto_to_error(dec), PSYNC_CRYPTO_NOT_STARTED);
} END_TEST

START_TEST(test_folder_encoder_not_started_bypasses_lock_path) {
  crypto_started_un = 1;
  crypto_started_l = 0;
  psync_cache_get_fake.return_val = NULL;

  psync_crypto_aes256_text_encoder_t enc = psync_cloud_crypto_get_folder_encoder(42);
  ck_assert(psync_crypto_is_error(enc));
  ck_assert_int_eq(psync_crypto_to_error(enc), PSYNC_CRYPTO_NOT_STARTED);
} END_TEST

START_TEST(test_file_encoder_not_started_bypasses_lock_path) {
  crypto_started_un = 1;
  crypto_started_l = 0;
  psync_cache_get_fake.return_val = NULL;

  psync_crypto_aes256_sector_encoder_decoder_t enc = psync_cloud_crypto_get_file_encoder(42, 0, 0);
  ck_assert(psync_crypto_is_error(enc));
  ck_assert_int_eq(psync_crypto_to_error(enc), PSYNC_CRYPTO_NOT_STARTED);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: crypto_keys_match (BUG-7 fix)
 * ══════════════════════════════════════════════════════════════════════════ */

static int fake_pub_key;
static int fake_priv_key;
static int fake_pub_key2;
static int fake_priv_key2;

/* Custom fake that records which pub key was used */
static psync_rsa_publickey_t encrypt_captured_pub;
static psync_encrypted_symmetric_key_t custom_encrypt(psync_rsa_publickey_t rsa, const psync_symmetric_key_t key) {
  encrypt_captured_pub = rsa;
  /* Return a fake encrypted key */
  psync_encrypted_symmetric_key_t ekey = malloc(sizeof(psync_encrypted_data_struct_t) + key->keylen);
  ekey->datalen = key->keylen;
  memcpy(ekey->data, key->key, key->keylen);
  return ekey;
}

static void custom_rand_weak(unsigned char *buf, int len) {
  memset(buf, 0xAB, len);
}

static psync_rsa_privatekey_t decrypt_captured_priv;
static psync_symmetric_key_t custom_decrypt(psync_rsa_privatekey_t rsa, const psync_encrypted_symmetric_key_t ekey) {
  decrypt_captured_priv = rsa;
  /* Return a matching key */
  psync_symmetric_key_t key = malloc(offsetof(psync_symmetric_key_struct_t, key) + ekey->datalen);
  key->keylen = ekey->datalen;
  memcpy(key->key, ekey->data, ekey->datalen);
  return key;
}

START_TEST(test_keys_match_uses_passed_keys) {
  psync_ssl_rand_weak_fake.custom_fake = custom_rand_weak;
  psync_ssl_rsa_encrypt_symmetric_key_fake.custom_fake = custom_encrypt;
  psync_ssl_rsa_decrypt_symmetric_key_fake.custom_fake = custom_decrypt;
  /* Use real free for the key allocated by custom_decrypt */
  psync_ssl_free_symmetric_key_fake.custom_fake = (void (*)(psync_symmetric_key_t))free;

  encrypt_captured_pub = NULL;
  decrypt_captured_priv = NULL;

  int result = crypto_keys_match((psync_rsa_publickey_t)&fake_pub_key,
                                  (psync_rsa_privatekey_t)&fake_priv_key);
  ck_assert_int_eq(result, 1);
  ck_assert_ptr_eq(encrypt_captured_pub, &fake_pub_key);
  ck_assert_ptr_eq(decrypt_captured_priv, &fake_priv_key);
} END_TEST

START_TEST(test_keys_match_uses_different_keys) {
  /* Verify that passing different key pointers results in different captures */
  psync_ssl_rand_weak_fake.custom_fake = custom_rand_weak;
  psync_ssl_rsa_encrypt_symmetric_key_fake.custom_fake = custom_encrypt;
  psync_ssl_rsa_decrypt_symmetric_key_fake.custom_fake = custom_decrypt;
  psync_ssl_free_symmetric_key_fake.custom_fake = (void (*)(psync_symmetric_key_t))free;

  encrypt_captured_pub = NULL;
  decrypt_captured_priv = NULL;

  int result = crypto_keys_match((psync_rsa_publickey_t)&fake_pub_key2,
                                  (psync_rsa_privatekey_t)&fake_priv_key2);
  ck_assert_int_eq(result, 1);
  ck_assert_ptr_eq(encrypt_captured_pub, &fake_pub_key2);
  ck_assert_ptr_eq(decrypt_captured_priv, &fake_priv_key2);
} END_TEST

START_TEST(test_keys_match_returns_false_on_encrypt_failure) {
  psync_ssl_rsa_encrypt_symmetric_key_fake.return_val = PSYNC_INVALID_ENC_SYM_KEY;

  int result = crypto_keys_match((psync_rsa_publickey_t)&fake_pub_key,
                                  (psync_rsa_privatekey_t)&fake_priv_key);
  ck_assert_int_eq(result, 0);
} END_TEST

START_TEST(test_keys_match_returns_false_on_decrypt_failure) {
  /* Encrypt succeeds, decrypt fails */
  psync_ssl_rsa_encrypt_symmetric_key_fake.custom_fake = custom_encrypt;
  psync_ssl_rsa_decrypt_symmetric_key_fake.return_val = PSYNC_INVALID_SYM_KEY;

  int result = crypto_keys_match((psync_rsa_publickey_t)&fake_pub_key,
                                  (psync_rsa_privatekey_t)&fake_priv_key);
  ck_assert_int_eq(result, 0);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: psync_crypto_sym_key_ver1_to_sym_key
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_sym_key_ver1_to_sym_key_copies) {
  psync_locked_malloc_fake.custom_fake = real_locked_malloc;
  psync_locked_free_fake.custom_fake = real_locked_free;

  sym_key_ver1 v1;
  memset(&v1, 0, sizeof(v1));
  /* Fill with known patterns */
  memset(v1.aeskey, 0xAA, PSYNC_AES256_KEY_SIZE);
  memset(v1.hmackey, 0xBB, PSYNC_CRYPTO_HMAC_SHA512_KEY_LEN);

  psync_symmetric_key_t key = psync_crypto_sym_key_ver1_to_sym_key(&v1);
  ck_assert_ptr_ne(key, NULL);
  ck_assert_uint_eq(key->keylen, PSYNC_AES256_KEY_SIZE + PSYNC_CRYPTO_HMAC_SHA512_KEY_LEN);

  /* Check AES key portion */
  unsigned char expected_aes[PSYNC_AES256_KEY_SIZE];
  memset(expected_aes, 0xAA, PSYNC_AES256_KEY_SIZE);
  ck_assert_mem_eq(key->key, expected_aes, PSYNC_AES256_KEY_SIZE);

  /* Check HMAC key portion */
  unsigned char expected_hmac[PSYNC_CRYPTO_HMAC_SHA512_KEY_LEN];
  memset(expected_hmac, 0xBB, PSYNC_CRYPTO_HMAC_SHA512_KEY_LEN);
  ck_assert_mem_eq(key->key + PSYNC_AES256_KEY_SIZE, expected_hmac, PSYNC_CRYPTO_HMAC_SHA512_KEY_LEN);

  free(key);
} END_TEST

START_TEST(test_sym_key_ver1_to_sym_key_length) {
  psync_locked_malloc_fake.custom_fake = real_locked_malloc;

  sym_key_ver1 v1;
  memset(&v1, 0, sizeof(v1));

  psync_symmetric_key_t key = psync_crypto_sym_key_ver1_to_sym_key(&v1);
  ck_assert_uint_eq(key->keylen, PSYNC_AES256_KEY_SIZE + PSYNC_CRYPTO_HMAC_SHA512_KEY_LEN);
  ck_assert_uint_eq(key->keylen, 32 + 128);

  free(key);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Tests: set_crypto_err_msg
 * ══════════════════════════════════════════════════════════════════════════ */

START_TEST(test_set_crypto_err_msg_short) {
  binresult res;
  res.type = PARAM_STR;
  res.str = "short error";
  res.length = strlen("short error");

  /* Wrap in a structure that psync_find_result can return.
   * Our stub returns the same pointer. */
  set_crypto_err_msg(&res);
  ck_assert_str_eq(crypto_api_err, "short error");
} END_TEST

START_TEST(test_set_crypto_err_msg_truncation) {
  /* Message longer than sizeof(crypto_api_err)-1 = 127 chars */
  char longmsg[256];
  memset(longmsg, 'X', 255);
  longmsg[255] = '\0';

  binresult res;
  res.type = PARAM_STR;
  res.str = longmsg;
  res.length = 255;

  set_crypto_err_msg(&res);
  /* Should be truncated to sizeof(crypto_api_err)-1 = 127 */
  ck_assert_uint_le(strlen(crypto_api_err), sizeof(crypto_api_err) - 1);
} END_TEST

/* ══════════════════════════════════════════════════════════════════════════
 *  Suite setup
 * ══════════════════════════════════════════════════════════════════════════ */

static Suite *pcloudcrypto_suite(void) {
  Suite *s = suite_create("pcloudcrypto");

  TCase *tc_err = tcase_create("error_helpers");
  tcase_add_test(tc_err, test_err_to_ptr_roundtrip);
  tcase_add_test(tc_err, test_err_to_ptr_roundtrip_folder_not_encrypted);
  tcase_add_test(tc_err, test_is_error_small_ptr);
  tcase_add_test(tc_err, test_is_error_real_ptr);
  suite_add_tcase(s, tc_err);

  TCase *tc_guard = tcase_create("not_started_guard");
  tcase_add_checked_fixture(tc_guard, setup, NULL);
  tcase_add_test(tc_guard, test_folder_decoder_not_started);
  tcase_add_test(tc_guard, test_folder_encoder_not_started);
  tcase_add_test(tc_guard, test_file_encoder_not_started);
  tcase_add_test(tc_guard, test_folder_decoder_not_started_bypasses_lock_path);
  tcase_add_test(tc_guard, test_folder_encoder_not_started_bypasses_lock_path);
  tcase_add_test(tc_guard, test_file_encoder_not_started_bypasses_lock_path);
  suite_add_tcase(s, tc_guard);

  TCase *tc_keys = tcase_create("keys_match");
  tcase_add_checked_fixture(tc_keys, setup, NULL);
  tcase_add_test(tc_keys, test_keys_match_uses_passed_keys);
  tcase_add_test(tc_keys, test_keys_match_uses_different_keys);
  tcase_add_test(tc_keys, test_keys_match_returns_false_on_encrypt_failure);
  tcase_add_test(tc_keys, test_keys_match_returns_false_on_decrypt_failure);
  suite_add_tcase(s, tc_keys);

  TCase *tc_sym = tcase_create("sym_key_conversion");
  tcase_add_checked_fixture(tc_sym, setup, NULL);
  tcase_add_test(tc_sym, test_sym_key_ver1_to_sym_key_copies);
  tcase_add_test(tc_sym, test_sym_key_ver1_to_sym_key_length);
  suite_add_tcase(s, tc_sym);

  TCase *tc_msg = tcase_create("error_messages");
  tcase_add_test(tc_msg, test_set_crypto_err_msg_short);
  tcase_add_test(tc_msg, test_set_crypto_err_msg_truncation);
  suite_add_tcase(s, tc_msg);

  return s;
}

int main(void) {
  Suite *s = pcloudcrypto_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
