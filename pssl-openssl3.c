/* Copyright (c) 2013 Anton Titov.
 * Copyright (c) 2013 pCloud Ltd.
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

/*
 * OpenSSL 3.x SSL provider for psynclib.
 *
 * This file implements the same psync_ssl_* interface as pssl-openssl.c but
 * uses only current (non-deprecated) OpenSSL 3.x APIs:
 *   - EVP_PKEY / EVP_PKEY_CTX for all asymmetric operations
 *   - EVP_MD_CTX for all hash operations (see pssl-openssl3.h macros)
 *   - TLS_client_method() + SSL_CTX_set_min_proto_version()
 *   - RAND_bytes() (RAND_pseudo_bytes was removed in 3.0)
 *   - No manual threading setup (OpenSSL 3.x is thread-safe by default)
 *   - ASN1_STRING_get0_data() instead of the deprecated ASN1_STRING_data()
 *
 * The AES encoder/decoder uses EVP_CIPHER_CTX in ECB mode (padding disabled).
 * OpenSSL 3.x applies AES-NI internally via the EVP layer.
 *
 * Binary RSA key format is wire-compatible with pssl-openssl.c:
 *   public  key  -> PKCS#1 RSAPublicKey  (i2d_PublicKey  / d2i_PublicKey)
 *   private key  -> PKCS#1 RSAPrivateKey (i2d_PrivateKey / d2i_PrivateKey)
 */

/* Ensure clangd (and other tools that don't receive Makefile -D flags)
 * resolves pssl.h to the correct provider header.  The #ifndef guard
 * avoids a redefinition warning when the Makefile also passes -DP_SSL_OPENSSL3. */
#ifndef P_SSL_OPENSSL3
#define P_SSL_OPENSSL3
#endif

#include "plibs.h"
#include "pssl.h"
#include "psynclib.h"
#include "psslcerts.h"
#include "psettings.h"
#include "pcache.h"
#include "ptimer.h"
#include "pmemlock.h"
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/tls1.h>
#include <ctype.h>

/* TLS 1.2 cipher suites (legacy string accepted by SSL_CTX_set_cipher_list) */
#define SSL_CIPHERS \
  "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:" \
  "DHE-RSA-AES256-GCM-SHA384:ECDH-RSA-AES256-GCM-SHA384:"      \
  "ECDHE-RSA-AES256-SHA384:DHE-RSA-AES256-SHA256:"              \
  "AES256-GCM-SHA384:AES256-SHA256"

/* TLS 1.3 cipher suites */
#define SSL_CIPHERS_TLS13 \
  "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256"

typedef struct {
  SSL *ssl;
  int isbroken;
  char cachekey[];
} ssl_connection_t;

static SSL_CTX *globalctx = NULL;

PSYNC_THREAD int psync_ssl_errno;

/* -------------------------------------------------------------------------
 * Initialisation
 *
 * OpenSSL 3.x initialises automatically on first use (since 1.1.0), so no
 * SSL_library_init / OpenSSL_add_all_* calls are needed.  Thread safety is
 * also handled internally - no locking callbacks required.
 * ------------------------------------------------------------------------- */

int psync_ssl_init(){
  BIO *bio;
  X509 *cert;
  psync_uint_t i;
  unsigned char seed[PSYNC_LHASH_DIGEST_LEN];

  globalctx = SSL_CTX_new(TLS_client_method());
  if (likely_log(globalctx)){
    /* Enforce TLS 1.2 as the minimum protocol version */
    SSL_CTX_set_min_proto_version(globalctx, TLS1_2_VERSION);

    if (unlikely_log(SSL_CTX_set_cipher_list(globalctx, SSL_CIPHERS) != 1)){
      SSL_CTX_free(globalctx);
      globalctx = NULL;
      return -1;
    }
    /* Add TLS 1.3 cipher suites */
    SSL_CTX_set_ciphersuites(globalctx, SSL_CIPHERS_TLS13);

    SSL_CTX_set_verify(globalctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_read_ahead(globalctx, 0);  /* readahead breaks SSL_pending */
    SSL_CTX_set_session_cache_mode(globalctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL);
    SSL_CTX_set_options(globalctx, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_mode(globalctx, SSL_MODE_RELEASE_BUFFERS);
    SSL_CTX_set_mode(globalctx, SSL_MODE_ENABLE_PARTIAL_WRITE);

    for (i = 0; i < ARRAY_SIZE(psync_ssl_trusted_certs); i++){
      bio = BIO_new(BIO_s_mem());
      BIO_puts(bio, psync_ssl_trusted_certs[i]);
      cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
      BIO_free(bio);
      if (likely_log(cert != NULL)){
        X509_STORE_add_cert(SSL_CTX_get_cert_store(globalctx), cert);
        X509_free(cert);
      }
    }
    do {
      psync_get_random_seed(seed, NULL, 0, 0);
      RAND_seed(seed, PSYNC_LHASH_DIGEST_LEN);
    } while (!RAND_status());
    return 0;
  }
  return -1;
}

/* -------------------------------------------------------------------------
 * Memory
 * ------------------------------------------------------------------------- */

void psync_ssl_memclean(void *ptr, size_t len){
  OPENSSL_cleanse(ptr, len);
}

/* -------------------------------------------------------------------------
 * SSL connection helpers
 * ------------------------------------------------------------------------- */

static void psync_set_ssl_error(ssl_connection_t *conn, int err){
  if (err == SSL_ERROR_WANT_READ)
    psync_ssl_errno = PSYNC_SSL_ERR_WANT_READ;
  else if (err == SSL_ERROR_WANT_WRITE)
    psync_ssl_errno = PSYNC_SSL_ERR_WANT_WRITE;
  else {
    psync_ssl_errno = PSYNC_SSL_ERR_UNKNOWN;
    conn->isbroken = 1;
    debug(D_NOTICE, "got error %d from OpenSSL: %s", err, ERR_error_string(err, NULL));
  }
}

static int psync_ssl_compare_cn_hostname(const char *cn, size_t cnlen,
                                          const char *hostname, size_t hostnamelen){
  if (cn[0] == '*' && cn[1] == '.')
    return cnlen <= hostnamelen &&
           !memcmp(cn + 1, hostname + hostnamelen - cnlen + 1, cnlen) &&
           !memchr(hostname, '.', hostnamelen - cnlen + 1);
  else
    return cnlen == hostnamelen && !memcmp(cn, hostname, cnlen);
}

static int psync_ssl_cn_match_hostname(X509 *cert, const char *hostname){
  X509_NAME *sname;
  X509_NAME_ENTRY *cnentry;
  ASN1_STRING *cnasn;
  const char *cnstr;
  size_t cnstrlen;
  int idx;

  sname = X509_get_subject_name(cert);
  if (unlikely_log(!sname))
    return -1;
  idx = X509_NAME_get_index_by_NID(sname, NID_commonName, -1);
  if (unlikely_log(idx < 0))
    return -1;
  cnentry = X509_NAME_get_entry(sname, idx);
  if (unlikely_log(!cnentry))
    return -1;
  cnasn = X509_NAME_ENTRY_get_data(cnentry);
  if (unlikely_log(!cnasn))
    return -1;
  /* ASN1_STRING_get0_data() is the non-deprecated replacement for
   * ASN1_STRING_data() introduced in OpenSSL 1.1.0.               */
  cnstr = (const char *)ASN1_STRING_get0_data(cnasn);
  if (unlikely_log(!cnstr))
    return -1;
  cnstrlen = strlen(cnstr);
  if (unlikely_log(ASN1_STRING_length(cnasn) != (int)cnstrlen))
    return -1;
  debug(D_NOTICE, "got certificate with commonName: %s", cnstr);
  if (psync_ssl_compare_cn_hostname(cnstr, cnstrlen, hostname, strlen(hostname)))
    return 0;
  debug(D_WARNING, "hostname %s does not match certificate common name %s", hostname, cnstr);
  return -1;
}

static int psync_ssl_verify_cert(SSL *ssl, const char *hostname){
  X509 *cert;
  int ret;
  if (unlikely_log(SSL_get_verify_result(ssl) != X509_V_OK))
    return -1;
  /* SSL_get1_peer_certificate() is the OpenSSL 3.x preferred name for
   * SSL_get_peer_certificate(); both increment the reference count.   */
  cert = SSL_get1_peer_certificate(ssl);
  if (unlikely_log(!cert))
    return -1;
  ret = psync_ssl_cn_match_hostname(cert, hostname);
  X509_free(cert);
  return ret;
}

static ssl_connection_t *psync_ssl_alloc_conn(SSL *ssl, const char *hostname){
  ssl_connection_t *conn;
  size_t len;
  len = strlen(hostname) + 1;
  conn = (ssl_connection_t *)psync_malloc(offsetof(ssl_connection_t, cachekey) + len + 4);
  conn->ssl = ssl;
  conn->isbroken = 0;
  memcpy(conn->cachekey, "SSLS", 4);
  memcpy(conn->cachekey + 4, hostname, len);
  return conn;
}

/* -------------------------------------------------------------------------
 * Public SSL connection API
 * ------------------------------------------------------------------------- */

int psync_ssl_connect(psync_socket_t sock, void **sslconn, const char *hostname){
  ssl_connection_t *conn;
  SSL *ssl;
  SSL_SESSION *sess;
  int res, err;

  ssl = SSL_new(globalctx);
  if (!ssl)
    return PRINT_RETURN_CONST(PSYNC_SSL_FAIL);
  SSL_set_fd(ssl, sock);
  conn = psync_ssl_alloc_conn(ssl, hostname);
  if ((sess = (SSL_SESSION *)psync_cache_get(conn->cachekey))){
    debug(D_NOTICE, "reusing cached session for %s", hostname);
    SSL_set_session(ssl, sess);
    SSL_SESSION_free(sess);
  }
  res = SSL_connect(ssl);
  if (res == 1){
    if (unlikely(psync_ssl_verify_cert(ssl, hostname)))
      goto fail;
    *sslconn = conn;
    if (IS_DEBUG && SSL_session_reused(ssl))
      debug(D_NOTICE, "successfully reused session");
    return PSYNC_SSL_SUCCESS;
  }
  err = SSL_get_error(ssl, res);
  psync_set_ssl_error(conn, err);
  if (likely_log(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)){
    *sslconn = conn;
    return PSYNC_SSL_NEED_FINISH;
  }
fail:
  SSL_free(ssl);
  psync_free(conn);
  return PRINT_RETURN_CONST(PSYNC_SSL_FAIL);
}

int psync_ssl_connect_finish(void *sslconn, const char *hostname){
  ssl_connection_t *conn;
  int res, err;

  conn = (ssl_connection_t *)sslconn;
  res = SSL_connect(conn->ssl);
  if (res == 1){
    if (unlikely(psync_ssl_verify_cert(conn->ssl, hostname)))
      goto fail;
    if (IS_DEBUG && SSL_session_reused(conn->ssl))
      debug(D_NOTICE, "successfully reused session");
    return PSYNC_SSL_SUCCESS;
  }
  err = SSL_get_error(conn->ssl, res);
  psync_set_ssl_error(conn, err);
  if (likely_log(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE))
    return PSYNC_SSL_NEED_FINISH;
fail:
  SSL_free(conn->ssl);
  psync_free(conn);
  return PRINT_RETURN_CONST(PSYNC_SSL_FAIL);
}

static void psync_ssl_free_session(void *ptr){
  SSL_SESSION_free((SSL_SESSION *)ptr);
}

int psync_ssl_shutdown(void *sslconn){
  ssl_connection_t *conn;
  SSL_SESSION *sess;
  int res, err;

  conn = (ssl_connection_t *)sslconn;
  sess = SSL_get1_session(conn->ssl);
  if (sess)
    psync_cache_add(conn->cachekey, sess, PSYNC_SSL_SESSION_CACHE_TIMEOUT,
                    psync_ssl_free_session, PSYNC_MAX_SSL_SESSIONS_PER_DOMAIN);
  if (conn->isbroken)
    goto noshutdown;
  res = SSL_shutdown(conn->ssl);
  if (res != -1)
    goto noshutdown;
  err = SSL_get_error(conn->ssl, res);
  psync_set_ssl_error(conn, err);
  if (likely_log(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE))
    return PSYNC_SSL_NEED_FINISH;
noshutdown:
  SSL_free(conn->ssl);
  psync_free(conn);
  return PSYNC_SSL_SUCCESS;
}

void psync_ssl_free(void *sslconn){
  ssl_connection_t *conn = (ssl_connection_t *)sslconn;
  SSL_free(conn->ssl);
  psync_free(conn);
}

int psync_ssl_pendingdata(void *sslconn){
  return SSL_pending(((ssl_connection_t *)sslconn)->ssl);
}

int psync_ssl_read(void *sslconn, void *buf, int num){
  ssl_connection_t *conn = (ssl_connection_t *)sslconn;
  int res, err;
  res = SSL_read(conn->ssl, buf, num);
  if (res >= 0)
    return res;
  err = SSL_get_error(conn->ssl, res);
  psync_set_ssl_error(conn, err);
  return PSYNC_SSL_FAIL;
}

int psync_ssl_write(void *sslconn, const void *buf, int num){
  ssl_connection_t *conn = (ssl_connection_t *)sslconn;
  int res, err;
  res = SSL_write(conn->ssl, buf, num);
  if (res >= 0)
    return res;
  err = SSL_get_error(conn->ssl, res);
  psync_set_ssl_error(conn, err);
  return PSYNC_SSL_FAIL;
}

/* -------------------------------------------------------------------------
 * Random number generation
 *
 * RAND_pseudo_bytes() was removed in OpenSSL 3.0.  Both strong and weak
 * paths now use RAND_bytes() - the distinction is meaningless on modern
 * systems where /dev/urandom is always seeded.
 * ------------------------------------------------------------------------- */

void psync_ssl_rand_strong(unsigned char *buf, int num){
  static int seeds = 0;
  int ret;

  if (seeds < 2){
    unsigned char seed[PSYNC_LHASH_DIGEST_LEN];
    psync_get_random_seed(seed, buf, num, 1);
    RAND_seed(seed, PSYNC_LHASH_DIGEST_LEN);
    seeds++;
  }
  ret = RAND_bytes(buf, num);
  if (unlikely(ret == 0)){
    unsigned char seed[PSYNC_LHASH_DIGEST_LEN];
    psync_uint_t cnt = 0;
    while (ret == 0 && cnt++ < 20){
      psync_get_random_seed(seed, NULL, 0, 0);
      RAND_seed(seed, PSYNC_LHASH_DIGEST_LEN);
      ret = RAND_bytes(buf, num);
    }
  }
  if (unlikely(ret != 1)){
    debug(D_CRITICAL, "could not generate %d random bytes, error %s, exiting",
          num, ERR_error_string(ERR_get_error(), NULL));
    exit(1);
  }
}

void psync_ssl_rand_weak(unsigned char *buf, int num){
  /* RAND_pseudo_bytes() was removed in OpenSSL 3.0; RAND_bytes() is always
   * cryptographically strong on modern platforms.                           */
  if (unlikely(RAND_bytes(buf, num) != 1)){
    debug(D_CRITICAL, "could not generate %d weak random bytes, error %s, exiting",
          num, ERR_error_string(ERR_get_error(), NULL));
    exit(1);
  }
}

/* -------------------------------------------------------------------------
 * RSA key generation and lifecycle
 *
 * OpenSSL 3.x deprecates the RSA_* low-level API.  All asymmetric operations
 * go through EVP_PKEY / EVP_PKEY_CTX.
 * ------------------------------------------------------------------------- */

psync_rsa_t psync_ssl_gen_rsa(int bits){
  EVP_PKEY_CTX *ctx;
  EVP_PKEY *pkey = NULL;
  unsigned char seed[PSYNC_LHASH_DIGEST_LEN];

  psync_get_random_seed(seed, seed, sizeof(seed), 0);
  RAND_seed(seed, PSYNC_LHASH_DIGEST_LEN);

  ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  if (unlikely_log(!ctx))
    return PSYNC_INVALID_RSA;
  if (unlikely_log(EVP_PKEY_keygen_init(ctx) <= 0))
    goto err;
  if (unlikely_log(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0))
    goto err;
  if (unlikely_log(EVP_PKEY_keygen(ctx, &pkey) <= 0))
    goto err;
  EVP_PKEY_CTX_free(ctx);
  return pkey;
err:
  EVP_PKEY_CTX_free(ctx);
  return PSYNC_INVALID_RSA;
}

void psync_ssl_free_rsa(psync_rsa_t rsa){
  EVP_PKEY_free(rsa);
}

psync_rsa_publickey_t psync_ssl_rsa_get_public(psync_rsa_t rsa){
  return EVP_PKEY_dup(rsa);
}

void psync_ssl_rsa_free_public(psync_rsa_publickey_t key){
  EVP_PKEY_free(key);
}

psync_rsa_privatekey_t psync_ssl_rsa_get_private(psync_rsa_t rsa){
  return EVP_PKEY_dup(rsa);
}

void psync_ssl_rsa_free_private(psync_rsa_privatekey_t key){
  EVP_PKEY_free(key);
}

/* -------------------------------------------------------------------------
 * RSA key serialisation / deserialisation
 *
 * Binary format is PKCS#1, wire-compatible with pssl-openssl.c:
 *   i2d_PublicKey(EVP_PKEY_RSA, ...)  == i2d_RSAPublicKey(RSA *, ...)
 *   i2d_PrivateKey(EVP_PKEY_RSA, ...) == i2d_RSAPrivateKey(RSA *, ...)
 * ------------------------------------------------------------------------- */

psync_binary_rsa_key_t psync_ssl_rsa_public_to_binary(psync_rsa_publickey_t rsa){
  psync_binary_rsa_key_t ret;
  unsigned char *p;
  int len;

  len = i2d_PublicKey(rsa, NULL);
  if (unlikely_log(len < 0))
    return PSYNC_INVALID_BIN_RSA;
  ret = psync_locked_malloc(offsetof(psync_encrypted_data_struct_t, data) + len);
  ret->datalen = len;
  p = ret->data;
  if (unlikely_log(i2d_PublicKey(rsa, &p) != len)){
    psync_locked_free(ret);
    return PSYNC_INVALID_BIN_RSA;
  }
  return ret;
}

psync_binary_rsa_key_t psync_ssl_rsa_private_to_binary(psync_rsa_privatekey_t rsa){
  psync_binary_rsa_key_t ret;
  unsigned char *p;
  int len;

  len = i2d_PrivateKey(rsa, NULL);
  if (unlikely_log(len < 0))
    return PSYNC_INVALID_BIN_RSA;
  ret = psync_locked_malloc(offsetof(psync_encrypted_data_struct_t, data) + len);
  ret->datalen = len;
  p = ret->data;
  if (unlikely_log(i2d_PrivateKey(rsa, &p) != len)){
    psync_locked_free(ret);
    return PSYNC_INVALID_BIN_RSA;
  }
  return ret;
}

psync_rsa_publickey_t psync_ssl_rsa_load_public(const unsigned char *keydata, size_t keylen){
  return d2i_PublicKey(EVP_PKEY_RSA, NULL, &keydata, (long)keylen);
}

psync_rsa_privatekey_t psync_ssl_rsa_load_private(const unsigned char *keydata, size_t keylen){
  return d2i_PrivateKey(EVP_PKEY_RSA, NULL, &keydata, (long)keylen);
}

psync_rsa_publickey_t psync_ssl_rsa_binary_to_public(psync_binary_rsa_key_t bin){
  return psync_ssl_rsa_load_public(bin->data, bin->datalen);
}

psync_rsa_privatekey_t psync_ssl_rsa_binary_to_private(psync_binary_rsa_key_t bin){
  return psync_ssl_rsa_load_private(bin->data, bin->datalen);
}

/* -------------------------------------------------------------------------
 * RSA encryption / decryption (OAEP padding, SHA-1 hash - matches original)
 * ------------------------------------------------------------------------- */

psync_encrypted_symmetric_key_t psync_ssl_rsa_encrypt_data(psync_rsa_publickey_t rsa,
                                                            const unsigned char *data,
                                                            size_t datalen){
  EVP_PKEY_CTX *ctx;
  psync_encrypted_symmetric_key_t ret;
  size_t outlen;

  ctx = EVP_PKEY_CTX_new(rsa, NULL);
  if (unlikely_log(!ctx))
    return PSYNC_INVALID_ENC_SYM_KEY;
  if (unlikely_log(EVP_PKEY_encrypt_init(ctx) <= 0))
    goto fail;
  if (unlikely_log(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0))
    goto fail;
  if (unlikely_log(EVP_PKEY_encrypt(ctx, NULL, &outlen, data, datalen) <= 0))
    goto fail;

  ret = psync_malloc(offsetof(psync_encrypted_data_struct_t, data) + outlen);
  if (unlikely_log(EVP_PKEY_encrypt(ctx, ret->data, &outlen, data, datalen) <= 0)){
    psync_free(ret);
    goto fail;
  }
  ret->datalen = outlen;
  EVP_PKEY_CTX_free(ctx);
  return ret;
fail:
  EVP_PKEY_CTX_free(ctx);
  return PSYNC_INVALID_ENC_SYM_KEY;
}

psync_symmetric_key_t psync_ssl_rsa_decrypt_data(psync_rsa_privatekey_t rsa,
                                                  const unsigned char *data,
                                                  size_t datalen){
  EVP_PKEY_CTX *ctx;
  unsigned char buff[2048];
  psync_symmetric_key_t ret;
  size_t outlen = sizeof(buff);

  ctx = EVP_PKEY_CTX_new(rsa, NULL);
  if (unlikely_log(!ctx))
    return PSYNC_INVALID_SYM_KEY;
  if (unlikely_log(EVP_PKEY_decrypt_init(ctx) <= 0))
    goto fail;
  if (unlikely_log(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0))
    goto fail;
  if (unlikely_log(EVP_PKEY_decrypt(ctx, buff, &outlen, data, datalen) <= 0)){
#if IS_DEBUG
    unsigned long e = ERR_get_error();
    debug(D_WARNING, "could not decrypt key, EVP_PKEY_decrypt error %lu: %s",
          e, ERR_error_string(e, NULL));
#endif
    goto fail;
  }
  EVP_PKEY_CTX_free(ctx);
  ret = psync_locked_malloc(offsetof(psync_symmetric_key_struct_t, key) + outlen);
  ret->keylen = outlen;
  memcpy(ret->key, buff, outlen);
  psync_ssl_memclean(buff, outlen);
  return ret;
fail:
  EVP_PKEY_CTX_free(ctx);
  return PSYNC_INVALID_SYM_KEY;
}

psync_encrypted_symmetric_key_t psync_ssl_rsa_encrypt_symmetric_key(psync_rsa_publickey_t rsa,
                                                                     const psync_symmetric_key_t key){
  return psync_ssl_rsa_encrypt_data(rsa, key->key, key->keylen);
}

psync_symmetric_key_t psync_ssl_rsa_decrypt_symmetric_key(psync_rsa_privatekey_t rsa,
                                                           const psync_encrypted_symmetric_key_t enckey){
  return psync_ssl_rsa_decrypt_data(rsa, enckey->data, enckey->datalen);
}

/* -------------------------------------------------------------------------
 * RSA-PSS signature over a pre-computed SHA-256 hash
 * ------------------------------------------------------------------------- */

psync_rsa_signature_t psync_ssl_rsa_sign_sha256_hash(psync_rsa_privatekey_t rsa,
                                                      const unsigned char *data){
  EVP_PKEY_CTX *ctx;
  psync_rsa_signature_t ret;
  size_t siglen;

  ctx = EVP_PKEY_CTX_new(rsa, NULL);
  if (unlikely_log(!ctx))
    return PSYNC_INVALID_BIN_RSA;
  if (unlikely_log(EVP_PKEY_sign_init(ctx) <= 0))
    goto fail;
  if (unlikely_log(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PSS_PADDING) <= 0))
    goto fail;
  if (unlikely_log(EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) <= 0))
    goto fail;
  /* First call: obtain required output length */
  if (unlikely_log(EVP_PKEY_sign(ctx, NULL, &siglen, data, PSYNC_SHA256_DIGEST_LEN) <= 0))
    goto fail;

  ret = psync_malloc(offsetof(psync_encrypted_data_struct_t, data) + siglen);
  if (unlikely_log(EVP_PKEY_sign(ctx, ret->data, &siglen, data, PSYNC_SHA256_DIGEST_LEN) <= 0)){
    psync_free(ret);
    goto fail;
  }
  ret->datalen = siglen;
  EVP_PKEY_CTX_free(ctx);
  return ret;
fail:
  EVP_PKEY_CTX_free(ctx);
  return PSYNC_INVALID_BIN_RSA;
}

/* -------------------------------------------------------------------------
 * Symmetric key derivation
 * ------------------------------------------------------------------------- */

psync_symmetric_key_t psync_ssl_gen_symmetric_key_from_pass(const char *password,
                                                             size_t keylen,
                                                             const unsigned char *salt,
                                                             size_t saltlen,
                                                             size_t iterations){
  psync_symmetric_key_t key =
      (psync_symmetric_key_t)psync_locked_malloc(keylen + offsetof(psync_symmetric_key_struct_t, key));
  key->keylen = keylen;
  PKCS5_PBKDF2_HMAC(password, strlen(password),
                    salt, saltlen,
                    iterations, EVP_sha512(),
                    keylen, key->key);
  return key;
}

char *psync_ssl_derive_password_from_passphrase(const char *username, const char *passphrase){
  unsigned char *usercopy;
  unsigned char usersha512[PSYNC_SHA512_DIGEST_LEN], passwordbin[32];
  size_t userlen, i, outlen;
  unsigned int hashlen;

  userlen = strlen(username);
  usercopy = psync_new_cnt(unsigned char, userlen);
  for (i = 0; i < userlen; i++){
    if ((unsigned char)username[i] <= 127)
      usercopy[i] = tolower((unsigned char)username[i]);
    else
      usercopy[i] = '*';
  }

  EVP_Digest(usercopy, userlen, usersha512, &hashlen, EVP_sha512(), NULL);
  psync_free(usercopy);

  PKCS5_PBKDF2_HMAC(passphrase, strlen(passphrase),
                    usersha512, PSYNC_SHA512_DIGEST_LEN,
                    5000, EVP_sha512(),
                    sizeof(passwordbin), passwordbin);

  usercopy = psync_base64_encode(passwordbin, sizeof(passwordbin), &outlen);
  return (char *)usercopy;
}

/* ── AES-256 encoder / decoder ─────────────────────────────────────────────
 * EVP_CIPHER_CTX in ECB mode, padding disabled.  The block-level operations
 * are inlined in pssl-openssl3.h.
 * ─────────────────────────────────────────────────────────────────────────── */

psync_aes256_encoder psync_ssl_aes256_create_encoder(psync_symmetric_key_t key){
  EVP_CIPHER_CTX *ctx;
  assert(key->keylen >= PSYNC_AES256_KEY_SIZE);
  ctx = EVP_CIPHER_CTX_new();
  if (unlikely_log(!ctx))
    return PSYNC_INVALID_ENCODER;
  EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, key->key, NULL);
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  return ctx;
}

void psync_ssl_aes256_free_encoder(psync_aes256_encoder enc){
  EVP_CIPHER_CTX_free(enc);
}

psync_aes256_decoder psync_ssl_aes256_create_decoder(psync_symmetric_key_t key){
  EVP_CIPHER_CTX *ctx;
  assert(key->keylen >= PSYNC_AES256_KEY_SIZE);
  ctx = EVP_CIPHER_CTX_new();
  if (unlikely_log(!ctx))
    return PSYNC_INVALID_ENCODER;
  EVP_DecryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, key->key, NULL);
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  return ctx;
}

void psync_ssl_aes256_free_decoder(psync_aes256_decoder dec){
  EVP_CIPHER_CTX_free(dec);
}

static psync_ssl_debug_callback_t debug_cb = NULL;
static void *debug_ctx = NULL;

void psync_ssl_set_log_threshold(int threshold){
  (void)threshold;
}

void psync_ssl_set_debug_callback(psync_ssl_debug_callback_t cb, void *ctx){
  debug_cb = cb;
  debug_ctx = ctx;
}
