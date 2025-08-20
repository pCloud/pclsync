/* Copyright (c) 2025 pCloud Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of pCloud Ltd nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL pCloud Ltd BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "plibs.h"
#include "pssl.h"
#include "psynclib.h"
#include "psslcerts.h"
#include "psettings.h"
#include "pcache.h"
#include "ptimer.h"
#include "pmemlock.h"
#include <pthread.h>
#include <ctype.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/pwdbased.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/aes.h>

#if defined(PSYNC_AES_HW_MSC)
#include <intrin.h>
#include <wmmintrin.h>
#endif

typedef struct {
  WC_RNG rng;
  pthread_mutex_t mutex;
} rng_context_locked;

typedef struct {
  WOLFSSL *ssl;
  psync_socket_t sock;
  int isbroken;
} ssl_connection_t;

static rng_context_locked psync_wolf_rng;
static pthread_mutex_t psync_wolf_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int psync_wolf_initialized = 0;
static WOLFSSL_CTX *psync_wolf_ctx = NULL;

PSYNC_THREAD int psync_ssl_errno;

#if defined(PSYNC_AES_HW)
int psync_ssl_hw_aes;
#endif

static psync_ssl_debug_callback_t debug_cb = NULL;
static void *debug_ctx = NULL;

/* Forward declarations */
static int psync_wolf_recv_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx);
static int psync_wolf_send_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx);

void psync_ssl_set_log_threshold(int threshold) {
  if (threshold >= 0) {
    wolfSSL_SetLoggingCb(NULL);
    wolfSSL_Debugging_ON();
  } else {
    wolfSSL_Debugging_OFF();
  }
}

void psync_ssl_set_debug_callback(psync_ssl_debug_callback_t cb, void *ctx) {
  debug_cb = cb;
  debug_ctx = ctx;
}

static int rng_gen_block(rng_context_locked *rng, unsigned char *output, word32 sz) {
  int ret;
  pthread_mutex_lock(&rng->mutex);
  ret = wc_RNG_GenerateBlock(&rng->rng, output, sz);
  pthread_mutex_unlock(&rng->mutex);
  return ret;
}

#if defined(PSYNC_AES_HW_GCC)
static int psync_ssl_detect_aes_hw() {
  uint32_t eax, ecx;
  eax = 1;
  __asm__("cpuid"
          : "=c"(ecx)
          : "a"(eax)
          : "%ebx", "%edx");
  ecx = (ecx >> 25) & 1;
  if (ecx)
    debug(D_NOTICE, "hardware AES support detected");
  else
    debug(D_NOTICE, "hardware AES support not detected");
  return ecx;
}
#elif defined(PSYNC_AES_HW_MSC)
static int psync_ssl_detect_aes_hw() {
  int info[4];
  int ret;
  __cpuid(info, 1);
  ret = (info[2] >> 25) & 1;
  if (ret)
    debug(D_NOTICE, "hardware AES support detected");
  else
    debug(D_NOTICE, "hardware AES support not detected");
  return ret;
}
#endif

int psync_ssl_init() {
  unsigned char seed[PSYNC_LHASH_DIGEST_LEN];
  psync_uint_t i;

  pthread_mutex_lock(&psync_wolf_init_mutex);
  if (psync_wolf_initialized) {
    pthread_mutex_unlock(&psync_wolf_init_mutex);
    return 0;
  }

#if defined(PSYNC_AES_HW)
  psync_ssl_hw_aes = psync_ssl_detect_aes_hw();
#else
  debug(D_NOTICE, "hardware AES is not supported for this compiler");
#endif

  if (pthread_mutex_init(&psync_wolf_rng.mutex, NULL)) {
    pthread_mutex_unlock(&psync_wolf_init_mutex);
    return PRINT_RETURN(-1);
  }

  wolfSSL_Init();

  psync_get_random_seed(seed, seed, sizeof(seed), 0);

  wc_InitRngNonce(&psync_wolf_rng.rng, seed, sizeof(seed));


  /* Create global SSL context */
  psync_wolf_ctx = wolfSSL_CTX_new(wolfTLS_client_method());
  if (!psync_wolf_ctx) {
    pthread_mutex_unlock(&psync_wolf_init_mutex);
    debug(D_ERROR, "wolfSSL_CTX_new failed");
    return PRINT_RETURN(-1);
  }

  wolfSSL_CTX_SetMinVersion(psync_wolf_ctx, WOLFSSL_TLSV1_2);
  wolfSSL_CTX_set_verify(psync_wolf_ctx, SSL_VERIFY_PEER, NULL);
  wolfSSL_CTX_SetIORecv(psync_wolf_ctx, psync_wolf_recv_cb);
  wolfSSL_CTX_SetIOSend(psync_wolf_ctx, psync_wolf_send_cb);

  /* Load trusted certificates */
  for (i = 0; i < ARRAY_SIZE(psync_ssl_trusted_certs); i++) {
    if (wolfSSL_CTX_load_verify_buffer(psync_wolf_ctx,
                                       (const unsigned char *)psync_ssl_trusted_certs[i],
                                       strlen(psync_ssl_trusted_certs[i]),
                                       WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {
      debug(D_ERROR, "failed to load certificate %lu", (unsigned long)i);
    }
  }

  psync_wolf_initialized = 1;
  pthread_mutex_unlock(&psync_wolf_init_mutex);

  return 0;
}

void psync_ssl_memclean(void *ptr, size_t len) {
  volatile unsigned char *p = ptr;
  while (len--)
    *p++ = 0;
}

static void psync_set_ssl_error(ssl_connection_t *conn, int err) {
  if (err == WOLFSSL_ERROR_WANT_READ)
    psync_ssl_errno = PSYNC_SSL_ERR_WANT_READ;
  else if (err == WOLFSSL_ERROR_WANT_WRITE)
    psync_ssl_errno = PSYNC_SSL_ERR_WANT_WRITE;
  else {
    psync_ssl_errno = PSYNC_SSL_ERR_UNKNOWN;
    conn->isbroken = 1;
    debug(D_NOTICE, "got error %d", err);
  }
}

static int psync_wolf_recv_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx) {
  ssl_connection_t *conn = (ssl_connection_t *)ctx;
  ssize_t ret;
  int err;

  ret = psync_read_socket(conn->sock, buf, sz);
  if (ret == -1) {
    err = psync_sock_err();
    if (err == P_WOULDBLOCK || err == P_AGAIN || err == P_INTR)
      return WOLFSSL_CBIO_ERR_WANT_READ;
    else
      return WOLFSSL_CBIO_ERR_GENERAL;
  }
  else
    return (int)ret;
}

static int psync_wolf_send_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx) {
  ssl_connection_t *conn = (ssl_connection_t *)ctx;
  ssize_t ret;
  int err;

  ret = psync_write_socket(conn->sock, buf, sz);
  if (ret == -1) {
    err = psync_sock_err();
    if (err == P_WOULDBLOCK || err == P_AGAIN || err == P_INTR)
      return WOLFSSL_CBIO_ERR_WANT_WRITE;
    else
      return WOLFSSL_CBIO_ERR_GENERAL;
  }
  else
    return (int)ret;
}

static int psync_ssl_check_peer_public_key(ssl_connection_t *conn) {
  WOLFSSL_X509 *cert;
  unsigned char pubkey_buf[2048];
  unsigned char sigbin[32];
  char sighex[66];
  int pubkey_len = sizeof(pubkey_buf);
  int i;

  cert = wolfSSL_get_peer_certificate(conn->ssl);
  if (!cert) {
    debug(D_WARNING, "wolfSSL_get_peer_certificate returned NULL");
    return -1;
  }

  /* Get the public key buffer from certificate */
  if (wolfSSL_X509_get_pubkey_buffer(cert, pubkey_buf, &pubkey_len) != WOLFSSL_SUCCESS) {
    wolfSSL_X509_free(cert);
    debug(D_WARNING, "Failed to get public key from certificate");
    return -1;
  }

  wolfSSL_X509_free(cert);

  /* Hash the public key DER data */
  wc_Sha256Hash(pubkey_buf, pubkey_len, sigbin);
  psync_binhex(sighex, sigbin, 32);
  sighex[64] = 0;

  /* Check against trusted public key fingerprints */
  for (i = 0; i < ARRAY_SIZE(psync_ssl_trusted_pk_sha256); i++) {
    if (!strcmp(sighex, psync_ssl_trusted_pk_sha256[i])) {
      debug(D_NOTICE, "Public key fingerprint matches trusted key %d", i);
      return 0;
    }
  }

  debug(D_ERROR, "got sha256hex of public key %s that does not match any approved fingerprint", sighex);
  return -1;
}

int psync_ssl_connect(psync_socket_t sock, void **sslconn, const char *hostname) {
  ssl_connection_t *conn;
  int ret;

  if (!psync_wolf_ctx) {
    debug(D_ERROR, "SSL not initialized");
    return PRINT_RETURN_CONST(PSYNC_SSL_FAIL);
  }

  conn = (ssl_connection_t *)psync_malloc(sizeof(ssl_connection_t));
  conn->sock = sock;
  conn->isbroken = 0;

  conn->ssl = wolfSSL_new(psync_wolf_ctx);
  if (!conn->ssl) {
    psync_free(conn);
    debug(D_ERROR, "wolfSSL_new failed");
    return PRINT_RETURN_CONST(PSYNC_SSL_FAIL);
  }

  wolfSSL_SetIOReadCtx(conn->ssl, conn);
  wolfSSL_SetIOWriteCtx(conn->ssl, conn);

  wolfSSL_set_tlsext_host_name(conn->ssl, hostname);

  ret = wolfSSL_connect(conn->ssl);
  if (ret == WOLFSSL_SUCCESS) {
    if (psync_ssl_check_peer_public_key(conn)) {
      wolfSSL_free(conn->ssl);
      psync_free(conn);
      return PRINT_RETURN_CONST(PSYNC_SSL_FAIL);
    }
    *sslconn = conn;
    return PSYNC_SSL_SUCCESS;
  }

  ret = wolfSSL_get_error(conn->ssl, ret);
  psync_set_ssl_error(conn, ret);

  if (likely_log(ret == WOLFSSL_ERROR_WANT_READ || ret == WOLFSSL_ERROR_WANT_WRITE)) {
    *sslconn = conn;
    return PSYNC_SSL_NEED_FINISH;
  }

  wolfSSL_free(conn->ssl);
  psync_free(conn);
  return PRINT_RETURN_CONST(PSYNC_SSL_FAIL);
}

int psync_ssl_connect_finish(void *sslconn, const char *hostname) {
  ssl_connection_t *conn = (ssl_connection_t *)sslconn;
  int ret;

  ret = wolfSSL_connect(conn->ssl);
  if (ret == WOLFSSL_SUCCESS) {
    if (psync_ssl_check_peer_public_key(conn)) {
      wolfSSL_free(conn->ssl);
      psync_free(conn);
      return PRINT_RETURN_CONST(PSYNC_SSL_FAIL);
    }
    return PSYNC_SSL_SUCCESS;
  }

  ret = wolfSSL_get_error(conn->ssl, ret);
  psync_set_ssl_error(conn, ret);

  if (likely_log(ret == WOLFSSL_ERROR_WANT_READ || ret == WOLFSSL_ERROR_WANT_WRITE))
    return PSYNC_SSL_NEED_FINISH;

  wolfSSL_free(conn->ssl);
  psync_free(conn);
  return PRINT_RETURN_CONST(PSYNC_SSL_FAIL);
}

int psync_ssl_shutdown(void *sslconn) {
  ssl_connection_t *conn = (ssl_connection_t *)sslconn;
  int ret;

  if (conn->isbroken)
    goto noshutdown;

  ret = wolfSSL_shutdown(conn->ssl);
  if (ret == WOLFSSL_SUCCESS)
    goto noshutdown;

  ret = wolfSSL_get_error(conn->ssl, ret);
  psync_set_ssl_error(conn, ret);

  if (likely_log(ret == WOLFSSL_ERROR_WANT_READ || ret == WOLFSSL_ERROR_WANT_WRITE))
    return PSYNC_SSL_NEED_FINISH;

noshutdown:
  wolfSSL_free(conn->ssl);
  psync_free(conn);
  return PSYNC_SSL_SUCCESS;
}

void psync_ssl_free(void *sslconn) {
  ssl_connection_t *conn = (ssl_connection_t *)sslconn;
  wolfSSL_free(conn->ssl);
  psync_free(conn);
}

int psync_ssl_pendingdata(void *sslconn) {
  return wolfSSL_pending(((ssl_connection_t *)sslconn)->ssl);
}

int psync_ssl_read(void *sslconn, void *buf, int num) {
  ssl_connection_t *conn = (ssl_connection_t *)sslconn;
  int res;

  res = wolfSSL_read(conn->ssl, buf, num);
  if (res > 0)
    return res;

  res = wolfSSL_get_error(conn->ssl, res);
  psync_set_ssl_error(conn, res);
  return PSYNC_SSL_FAIL;
}

int psync_ssl_write(void *sslconn, const void *buf, int num) {
  ssl_connection_t *conn = (ssl_connection_t *)sslconn;
  int res;

  res = wolfSSL_write(conn->ssl, buf, num);
  if (res > 0)
    return res;

  res = wolfSSL_get_error(conn->ssl, res);
  psync_set_ssl_error(conn, res);
  return PSYNC_SSL_FAIL;
}

void psync_ssl_rand_strong(unsigned char *buf, int num) {
  if (unlikely(rng_gen_block(&psync_wolf_rng, buf, num))) {
    debug(D_CRITICAL, "could not generate %d random bytes, exiting", num);
    abort();
  }
}

void psync_ssl_rand_weak(unsigned char *buf, int num) {
  psync_ssl_rand_strong(buf, num);
}

psync_rsa_t psync_ssl_gen_rsa(int bits) {
  RsaKey *key;
  int ret;

  key = psync_new(RsaKey);
  wc_InitRsaKey(key, NULL);

  ret = wc_MakeRsaKey(key, bits, 65537, &psync_wolf_rng.rng);
  if (ret != 0) {
    wc_FreeRsaKey(key);
    psync_free(key);
    return PSYNC_INVALID_RSA;
  }
  return key;
}

void psync_ssl_free_rsa(psync_rsa_t rsa) {
  wc_FreeRsaKey(rsa);
  psync_free(rsa);
}

psync_rsa_publickey_t psync_ssl_rsa_get_public(psync_rsa_t rsa) {
  RsaKey *pub;
  byte e[256], n[4096];
  word32 eSz = sizeof(e), nSz = sizeof(n);

  if (wc_RsaFlattenPublicKey(rsa, e, &eSz, n, &nSz) != 0) {
    debug(D_WARNING, "wc_RsaFlattenPublicKey failed");
    return PSYNC_INVALID_RSA;
  }

  pub = psync_new(RsaKey);
  wc_InitRsaKey(pub, NULL);

  if (wc_RsaPublicKeyDecodeRaw(n, nSz, e, eSz, pub) != 0) {
    wc_FreeRsaKey(pub);
    psync_free(pub);
    return PSYNC_INVALID_RSA;
  }

  return pub;
}

void psync_ssl_rsa_free_public(psync_rsa_publickey_t key) {
  psync_ssl_free_rsa(key);
}

psync_rsa_privatekey_t psync_ssl_rsa_get_private(psync_rsa_t rsa) {
  RsaKey *priv;
  byte der[4096];
  word32 derSz = sizeof(der);
  word32 idx = 0;
  int len;

  priv = psync_new(RsaKey);
  wc_InitRsaKey(priv, NULL);

  if ((len=wc_RsaKeyToDer(rsa, der, derSz)) <= 0 ||
      wc_RsaPrivateKeyDecode(der, &idx, priv, len) != 0) {
    wc_FreeRsaKey(priv);
    psync_free(priv);
    return PSYNC_INVALID_RSA;
  }

  return priv;
}

void psync_ssl_rsa_free_private(psync_rsa_privatekey_t key) {
  psync_ssl_free_rsa(key);
}

psync_binary_rsa_key_t psync_ssl_rsa_public_to_binary(psync_rsa_publickey_t rsa) {
  byte der[4096];
  int derSz;
  psync_binary_rsa_key_t ret;

  derSz = wc_RsaPublicKeyDerSize(rsa, 1);
  if (derSz <= 0 || derSz > sizeof(der))
    return PSYNC_INVALID_BIN_RSA;

  if (wc_RsaKeyToPublicDer(rsa, der, derSz) <= 0)
    return PSYNC_INVALID_BIN_RSA;

  ret = psync_locked_malloc(offsetof(psync_encrypted_data_struct_t, data) + derSz);
  ret->datalen = derSz;
  memcpy(ret->data, der, derSz);

  return ret;
}

psync_binary_rsa_key_t psync_ssl_rsa_private_to_binary(psync_rsa_privatekey_t rsa) {
  byte der[4096];
  int derSz;
  psync_binary_rsa_key_t ret;

  derSz = wc_RsaKeyToDer(rsa, der, sizeof(der));
  if (derSz <= 0)
    return PSYNC_INVALID_BIN_RSA;

  ret = psync_locked_malloc(offsetof(psync_encrypted_data_struct_t, data) + derSz);
  ret->datalen = derSz;
  memcpy(ret->data, der, derSz);
  psync_ssl_memclean(der, derSz);

  return ret;
}

psync_rsa_publickey_t psync_ssl_rsa_load_public(const unsigned char *keydata, size_t keylen) {
  RsaKey *key;
  word32 idx = 0;

  key = psync_new(RsaKey);
  wc_InitRsaKey(key, NULL);

  if (wc_RsaPublicKeyDecode(keydata, &idx, key, keylen) != 0) {
    wc_FreeRsaKey(key);
    psync_free(key);
    return PSYNC_INVALID_RSA;
  }

  return key;
}

psync_rsa_privatekey_t psync_ssl_rsa_load_private(const unsigned char *keydata, size_t keylen) {
  RsaKey *key;
  word32 idx = 0;

  key = psync_new(RsaKey);
  wc_InitRsaKey(key, NULL);

  if (wc_RsaPrivateKeyDecode(keydata, &idx, key, keylen) != 0) {
    wc_FreeRsaKey(key);
    psync_free(key);
    return PSYNC_INVALID_RSA;
  }

  if (wc_RsaSetRNG(key, &psync_wolf_rng.rng) != 0) {
    wc_FreeRsaKey(key);
    psync_free(key);
    return PSYNC_INVALID_RSA;
  }

  return key;
}

psync_rsa_publickey_t psync_ssl_rsa_binary_to_public(psync_binary_rsa_key_t bin) {
  return psync_ssl_rsa_load_public(bin->data, bin->datalen);
}

psync_rsa_privatekey_t psync_ssl_rsa_binary_to_private(psync_binary_rsa_key_t bin) {
  return psync_ssl_rsa_load_private(bin->data, bin->datalen);
}

psync_symmetric_key_t psync_ssl_gen_symmetric_key_from_pass(const char *password, size_t keylen,
                                                             const unsigned char *salt, size_t saltlen,
                                                             size_t iterations) {
  psync_symmetric_key_t key = (psync_symmetric_key_t)psync_locked_malloc(keylen+offsetof(psync_symmetric_key_struct_t, key));
  key->keylen = keylen;

  wc_PBKDF2(key->key, (const byte *)password, strlen(password),
           salt, saltlen, iterations, keylen, WC_SHA512);

  return key;
}

char *psync_ssl_derive_password_from_passphrase(const char *username, const char *passphrase) {
  unsigned char *usercopy;
  unsigned char usersha512[PSYNC_SHA512_DIGEST_LEN], passwordbin[32];
  size_t userlen, i;

  userlen = strlen(username);
  usercopy = psync_new_cnt(unsigned char, userlen);

  for (i = 0; i < userlen; i++)
    if ((unsigned char)username[i] <= 127)
      usercopy[i] = tolower((unsigned char)username[i]);
    else
      usercopy[i] = '*';

  wc_Sha512Hash(usercopy, userlen, usersha512);
  psync_free(usercopy);

  wc_PBKDF2(passwordbin, (const byte *)passphrase, strlen(passphrase),
           usersha512, PSYNC_SHA512_DIGEST_LEN, 5000, sizeof(passwordbin), WC_SHA512);

  usercopy = psync_base64_encode(passwordbin, sizeof(passwordbin), &userlen);
  return (char *)usercopy;
}

psync_encrypted_symmetric_key_t psync_ssl_rsa_encrypt_data(psync_rsa_publickey_t rsa,
                                                           const unsigned char *data, size_t datalen) {
  psync_encrypted_symmetric_key_t ret;
  int keySize;

  keySize = wc_RsaEncryptSize(rsa);
  if (keySize <= 0) {
    debug(D_WARNING, "wc_RsaEncryptSize failed");
    return PSYNC_INVALID_ENC_SYM_KEY;
  }

  ret = (psync_encrypted_symmetric_key_t)psync_malloc(offsetof(psync_encrypted_data_struct_t, data) + keySize);

  if (wc_RsaPublicEncrypt_ex(data, datalen, ret->data, keySize, rsa,
                             &psync_wolf_rng.rng, WC_RSA_OAEP_PAD, WC_HASH_TYPE_SHA,
                             WC_MGF1SHA1, NULL, 0) <= 0) {
    psync_free(ret);
    debug(D_WARNING, "wc_RsaPublicEncrypt_ex failed");
    return PSYNC_INVALID_ENC_SYM_KEY;
  }

  ret->datalen = keySize;
  return ret;
}

psync_symmetric_key_t psync_ssl_rsa_decrypt_data(psync_rsa_privatekey_t rsa,
                                                 const unsigned char *data, size_t datalen) {
  unsigned char buff[512];
  psync_symmetric_key_t ret;
  int rret=wc_RsaPrivateDecrypt_ex(data, datalen, buff, sizeof(buff), rsa,
                              WC_RSA_OAEP_PAD, WC_HASH_TYPE_SHA,
                              WC_MGF1SHA1, NULL, 0);
  if (rret<0) {
    debug(D_WARNING, "wc_RsaPrivateDecrypt_ex failed with %d", rret);
    return PSYNC_INVALID_SYM_KEY;
  }

  ret = (psync_symmetric_key_t)psync_locked_malloc(offsetof(psync_symmetric_key_struct_t, key) + rret);
  ret->keylen = rret;
  memcpy(ret->key, buff, rret);
  psync_ssl_memclean(buff, rret);

  return ret;
}

psync_encrypted_symmetric_key_t psync_ssl_rsa_encrypt_symmetric_key(psync_rsa_publickey_t rsa,
                                                                    const psync_symmetric_key_t key) {
  return psync_ssl_rsa_encrypt_data(rsa, key->key, key->keylen);
}

psync_symmetric_key_t psync_ssl_rsa_decrypt_symmetric_key(psync_rsa_privatekey_t rsa,
                                                          const psync_encrypted_symmetric_key_t enckey) {
  return psync_ssl_rsa_decrypt_data(rsa, enckey->data, enckey->datalen);
}

psync_aes256_encoder psync_ssl_aes256_create_encoder(psync_symmetric_key_t key) {
  Aes *aes;
  assert(key->keylen >= PSYNC_AES256_KEY_SIZE);

  aes = psync_new(Aes);
  wc_AesInit(aes, NULL, INVALID_DEVID);
  wc_AesSetKey(aes, key->key, PSYNC_AES256_KEY_SIZE, NULL, AES_ENCRYPTION);

  return aes;
}

void psync_ssl_aes256_free_encoder(psync_aes256_encoder aes) {
  wc_AesFree(aes);
  psync_ssl_memclean(aes, sizeof(Aes));
  psync_free(aes);
}

psync_aes256_encoder psync_ssl_aes256_create_decoder(psync_symmetric_key_t key) {
  Aes *aes;
  assert(key->keylen >= PSYNC_AES256_KEY_SIZE);

  aes = psync_new(Aes);
  wc_AesInit(aes, NULL, INVALID_DEVID);
  wc_AesSetKey(aes, key->key, PSYNC_AES256_KEY_SIZE, NULL, AES_DECRYPTION);

  return aes;
}

void psync_ssl_aes256_free_decoder(psync_aes256_encoder aes) {
  wc_AesFree(aes);
  psync_ssl_memclean(aes, sizeof(Aes));
  psync_free(aes);
}

psync_rsa_signature_t psync_ssl_rsa_sign_sha256_hash(psync_rsa_privatekey_t rsa,
                                                      const unsigned char *data) {
  psync_rsa_signature_t ret;
  word32 sigLen;
  int keySize;

  keySize = wc_RsaEncryptSize(rsa);
  if (keySize <= 0)
    return (psync_rsa_signature_t)(void *)PERROR_NO_MEMORY;

  ret = (psync_rsa_signature_t)psync_malloc(offsetof(psync_encrypted_data_struct_t, data) + keySize);
  if (!ret)
    return (psync_rsa_signature_t)(void *)PERROR_NO_MEMORY;

  sigLen = keySize;

  if ((keySize=wc_RsaPSS_Sign(data, PSYNC_SHA256_DIGEST_LEN, ret->data, sigLen, WC_HASH_TYPE_SHA256,
                     WC_MGF1SHA256, rsa, &psync_wolf_rng.rng)) <= 0) {
    debug(D_WARNING, "wc_RsaPSS_Sign failed with %d (%p %p %p)", keySize, data, ret->data, rsa);
    psync_free(ret);
    return (psync_rsa_signature_t)(void *)PERROR_SSL_INIT_FAILED;
  }
  ret->datalen = keySize;

  return ret;
}

#if defined(PSYNC_AES_HW_GCC)

#define SSE2FUNC __attribute__((__target__("sse2")))

#define AESDEC      ".byte 0x66,0x0F,0x38,0xDE,"
#define AESDECLAST  ".byte 0x66,0x0F,0x38,0xDF,"
#define AESENC      ".byte 0x66,0x0F,0x38,0xDC,"
#define AESENCLAST  ".byte 0x66,0x0F,0x38,0xDD,"

#define xmm0_xmm1   "0xC8"
#define xmm0_xmm2   "0xD0"
#define xmm0_xmm3   "0xD8"
#define xmm0_xmm4   "0xE0"
#define xmm0_xmm5   "0xE8"
#define xmm1_xmm0   "0xC1"
#define xmm1_xmm2   "0xD1"
#define xmm1_xmm3   "0xD9"
#define xmm1_xmm4   "0xE1"
#define xmm1_xmm5   "0xE9"

SSE2FUNC void psync_aes256_encode_block_hw(psync_aes256_encoder enc, const unsigned char *src, unsigned char *dst) {
  asm("movdqu (%0), %%xmm0\n"
      "lea 16(%0), %0\n"
      "movdqa (%1), %%xmm1\n"
      "dec %3\n"
      "pxor %%xmm0, %%xmm1\n"
      "movdqu (%0), %%xmm0\n"
      "1:\n"
      "lea 16(%0), %0\n"
      "dec %3\n"
      AESENC xmm0_xmm1 "\n"
      "movdqu (%0), %%xmm0\n"
      "jnz 1b\n"
      AESENCLAST xmm0_xmm1 "\n"
      "movdqa %%xmm1, (%2)\n"
      :
      : "r" (enc->key), "r" (src), "r" (dst), "r" (enc->rounds)
      : "memory", "cc", "xmm0", "xmm1"
  );
}

SSE2FUNC void psync_aes256_decode_block_hw(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst) {
  asm("movdqu (%0), %%xmm0\n"
      "lea 16(%0), %0\n"
      "movdqa (%1), %%xmm1\n"
      "dec %3\n"
      "pxor %%xmm0, %%xmm1\n"
      "movdqu (%0), %%xmm0\n"
      "1:\n"
      "lea 16(%0), %0\n"
      "dec %3\n"
      AESDEC xmm0_xmm1 "\n"
      "movdqu (%0), %%xmm0\n"
      "jnz 1b\n"
      AESDECLAST xmm0_xmm1 "\n"
      "movdqa %%xmm1, (%2)\n"
      :
      : "r" (enc->key), "r" (src), "r" (dst), "r" (enc->rounds)
      : "memory", "cc", "xmm0", "xmm1"
  );
}

SSE2FUNC void psync_aes256_encode_2blocks_consec_hw(psync_aes256_encoder enc, const unsigned char *src, unsigned char *dst) {
  asm("movdqu (%0), %%xmm0\n"
      "lea 16(%0), %0\n"
      "movdqa (%1), %%xmm1\n"
      "movdqa 16(%1), %%xmm2\n"
      "dec %3\n"
      "pxor %%xmm0, %%xmm1\n"
      "pxor %%xmm0, %%xmm2\n"
      "movdqu (%0), %%xmm0\n"
      "1:\n"
      "lea 16(%0), %0\n"
      "dec %3\n"
      AESENC xmm0_xmm1 "\n"
      AESENC xmm0_xmm2 "\n"
      "movdqu (%0), %%xmm0\n"
      "jnz 1b\n"
      AESENCLAST xmm0_xmm1 "\n"
      AESENCLAST xmm0_xmm2 "\n"
      "movdqa %%xmm1, (%2)\n"
      "movdqa %%xmm2, 16(%2)\n"
      :
      : "r" (enc->key), "r" (src), "r" (dst), "r" (enc->rounds)
      : "memory", "cc", "xmm0", "xmm1", "xmm2"
  );
}

SSE2FUNC void psync_aes256_decode_2blocks_consec_hw(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst) {
  asm("movdqu (%0), %%xmm0\n"
      "lea 16(%0), %0\n"
      "movdqa (%1), %%xmm1\n"
      "movdqa 16(%1), %%xmm2\n"
      "dec %3\n"
      "pxor %%xmm0, %%xmm1\n"
      "pxor %%xmm0, %%xmm2\n"
      "movdqu (%0), %%xmm0\n"
      "1:\n"
      "lea 16(%0), %0\n"
      "dec %3\n"
      AESDEC xmm0_xmm1 "\n"
      AESDEC xmm0_xmm2 "\n"
      "movdqu (%0), %%xmm0\n"
      "jnz 1b\n"
      AESDECLAST xmm0_xmm1 "\n"
      AESDECLAST xmm0_xmm2 "\n"
      "movdqa %%xmm1, (%2)\n"
      "movdqa %%xmm2, 16(%2)\n"
      :
      : "r" (enc->key), "r" (src), "r" (dst), "r" (enc->rounds)
      : "memory", "cc", "xmm0", "xmm1", "xmm2"
  );
}

SSE2FUNC void psync_aes256_decode_4blocks_consec_xor_hw(psync_aes256_decoder enc, const unsigned char *src,
                                                        unsigned char *dst, unsigned char *bxor) {
  asm("movdqu (%0), %%xmm0\n"
      "lea 16(%0), %0\n"
      "movdqa (%1), %%xmm1\n"
      "movdqa 16(%1), %%xmm2\n"
      "movdqa 32(%1), %%xmm3\n"
      "movdqa 48(%1), %%xmm4\n"
      "dec %4\n"
      "pxor %%xmm0, %%xmm1\n"
      "pxor %%xmm0, %%xmm2\n"
      "pxor %%xmm0, %%xmm3\n"
      "pxor %%xmm0, %%xmm4\n"
      "movdqu (%0), %%xmm0\n"
      "1:\n"
      "lea 16(%0), %0\n"
      "dec %4\n"
      AESDEC xmm0_xmm1 "\n"
      AESDEC xmm0_xmm2 "\n"
      AESDEC xmm0_xmm3 "\n"
      AESDEC xmm0_xmm4 "\n"
      "movdqu (%0), %%xmm0\n"
      "jnz 1b\n"
      AESDECLAST xmm0_xmm1 "\n"
      AESDECLAST xmm0_xmm2 "\n"
      AESDECLAST xmm0_xmm3 "\n"
      AESDECLAST xmm0_xmm4 "\n"
      "movdqa (%3), %%xmm0\n"
      "pxor %%xmm0, %%xmm1\n"
      "movdqa 16(%3), %%xmm0\n"
      "pxor %%xmm0, %%xmm2\n"
      "movdqa 32(%3), %%xmm0\n"
      "pxor %%xmm0, %%xmm3\n"
      "movdqa 48(%3), %%xmm0\n"
      "pxor %%xmm0, %%xmm4\n"
      "movdqa %%xmm1, (%2)\n"
      "movdqa %%xmm2, 16(%2)\n"
      "movdqa %%xmm3, 32(%2)\n"
      "movdqa %%xmm4, 48(%2)\n"
      :
      : "r" (enc->key), "r" (src), "r" (dst), "r" (bxor), "r" (enc->rounds)
      : "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4"
  );
}

#elif defined(PSYNC_AES_HW_MSC)

void psync_aes256_encode_block_hw(psync_aes256_encoder enc, const unsigned char *src, unsigned char *dst) {
  __m128i block = _mm_loadu_si128((const __m128i *)src);
  __m128i *key_schedule = (__m128i *)enc->key;
  int i;

  block = _mm_xor_si128(block, key_schedule[0]);
  for (i = 1; i < enc->rounds; i++)
    block = _mm_aesenc_si128(block, key_schedule[i]);
  block = _mm_aesenclast_si128(block, key_schedule[enc->rounds]);

  _mm_storeu_si128((__m128i *)dst, block);
}

void psync_aes256_decode_block_hw(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst) {
  __m128i block = _mm_loadu_si128((const __m128i *)src);
  __m128i *key_schedule = (__m128i *)enc->key;
  int i;

  block = _mm_xor_si128(block, key_schedule[0]);
  for (i = 1; i < enc->rounds; i++)
    block = _mm_aesdec_si128(block, key_schedule[i]);
  block = _mm_aesdeclast_si128(block, key_schedule[enc->rounds]);

  _mm_storeu_si128((__m128i *)dst, block);
}

void psync_aes256_encode_2blocks_consec_hw(psync_aes256_encoder enc, const unsigned char *src, unsigned char *dst) {
  __m128i block1 = _mm_loadu_si128((const __m128i *)src);
  __m128i block2 = _mm_loadu_si128((const __m128i *)(src + 16));
  __m128i *key_schedule = (__m128i *)enc->key;
  int i;

  block1 = _mm_xor_si128(block1, key_schedule[0]);
  block2 = _mm_xor_si128(block2, key_schedule[0]);

  for (i = 1; i < enc->rounds; i++) {
    block1 = _mm_aesenc_si128(block1, key_schedule[i]);
    block2 = _mm_aesenc_si128(block2, key_schedule[i]);
  }

  block1 = _mm_aesenclast_si128(block1, key_schedule[enc->rounds]);
  block2 = _mm_aesenclast_si128(block2, key_schedule[enc->rounds]);

  _mm_storeu_si128((__m128i *)dst, block1);
  _mm_storeu_si128((__m128i *)(dst + 16), block2);
}

void psync_aes256_decode_2blocks_consec_hw(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst) {
  __m128i block1 = _mm_loadu_si128((const __m128i *)src);
  __m128i block2 = _mm_loadu_si128((const __m128i *)(src + 16));
  __m128i *key_schedule = (__m128i *)enc->key;
  int i;

  block1 = _mm_xor_si128(block1, key_schedule[0]);
  block2 = _mm_xor_si128(block2, key_schedule[0]);

  for (i = 1; i < enc->rounds; i++) {
    block1 = _mm_aesdec_si128(block1, key_schedule[i]);
    block2 = _mm_aesdec_si128(block2, key_schedule[i]);
  }

  block1 = _mm_aesdeclast_si128(block1, key_schedule[enc->rounds]);
  block2 = _mm_aesdeclast_si128(block2, key_schedule[enc->rounds]);

  _mm_storeu_si128((__m128i *)dst, block1);
  _mm_storeu_si128((__m128i *)(dst + 16), block2);
}

void psync_aes256_decode_4blocks_consec_xor_hw(psync_aes256_decoder enc, const unsigned char *src,
                                               unsigned char *dst, unsigned char *bxor) {
  __m128i block1 = _mm_loadu_si128((const __m128i *)src);
  __m128i block2 = _mm_loadu_si128((const __m128i *)(src + 16));
  __m128i block3 = _mm_loadu_si128((const __m128i *)(src + 32));
  __m128i block4 = _mm_loadu_si128((const __m128i *)(src + 48));
  __m128i *key_schedule = (__m128i *)enc->key;
  int i;

  block1 = _mm_xor_si128(block1, key_schedule[0]);
  block2 = _mm_xor_si128(block2, key_schedule[0]);
  block3 = _mm_xor_si128(block3, key_schedule[0]);
  block4 = _mm_xor_si128(block4, key_schedule[0]);

  for (i = 1; i < enc->rounds; i++) {
    block1 = _mm_aesdec_si128(block1, key_schedule[i]);
    block2 = _mm_aesdec_si128(block2, key_schedule[i]);
    block3 = _mm_aesdec_si128(block3, key_schedule[i]);
    block4 = _mm_aesdec_si128(block4, key_schedule[i]);
  }

  block1 = _mm_aesdeclast_si128(block1, key_schedule[enc->rounds]);
  block2 = _mm_aesdeclast_si128(block2, key_schedule[enc->rounds]);
  block3 = _mm_aesdeclast_si128(block3, key_schedule[enc->rounds]);
  block4 = _mm_aesdeclast_si128(block4, key_schedule[enc->rounds]);

  block1 = _mm_xor_si128(block1, _mm_loadu_si128((const __m128i *)bxor));
  block2 = _mm_xor_si128(block2, _mm_loadu_si128((const __m128i *)(bxor + 16)));
  block3 = _mm_xor_si128(block3, _mm_loadu_si128((const __m128i *)(bxor + 32)));
  block4 = _mm_xor_si128(block4, _mm_loadu_si128((const __m128i *)(bxor + 48)));

  _mm_storeu_si128((__m128i *)dst, block1);
  _mm_storeu_si128((__m128i *)(dst + 16), block2);
  _mm_storeu_si128((__m128i *)(dst + 32), block3);
  _mm_storeu_si128((__m128i *)(dst + 48), block4);
}

#endif

void psync_aes256_decode_4blocks_consec_xor_sw(psync_aes256_decoder enc, const unsigned char *src,
                                               unsigned char *dst, unsigned char *bxor) {
  unsigned long i;
  wc_AesDecryptDirect(enc, dst, src);
  wc_AesDecryptDirect(enc, dst + PSYNC_AES256_BLOCK_SIZE, src + PSYNC_AES256_BLOCK_SIZE);
  wc_AesDecryptDirect(enc, dst + PSYNC_AES256_BLOCK_SIZE * 2, src + PSYNC_AES256_BLOCK_SIZE * 2);
  wc_AesDecryptDirect(enc, dst + PSYNC_AES256_BLOCK_SIZE * 3, src + PSYNC_AES256_BLOCK_SIZE * 3);

  for (i = 0; i < PSYNC_AES256_BLOCK_SIZE * 4 / sizeof(unsigned long); i++)
    ((unsigned long *)dst)[i] ^= ((unsigned long *)bxor)[i];
}
