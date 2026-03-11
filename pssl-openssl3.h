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

#ifndef _PSYNC_OPENSSL3_H
#define _PSYNC_OPENSSL3_H

#include "pcompiler.h"
#include <openssl/evp.h>

/* -------------------------------------------------------------------------
 * Asymmetric key types - all through EVP_PKEY in OpenSSL 3.x
 * ------------------------------------------------------------------------- */

typedef EVP_PKEY *psync_rsa_t;
typedef EVP_PKEY *psync_rsa_publickey_t;
typedef EVP_PKEY *psync_rsa_privatekey_t;

typedef struct {
  size_t keylen;
  unsigned char key[];
} psync_symmetric_key_struct_t, *psync_symmetric_key_t;

#define PSYNC_INVALID_RSA      NULL
#define PSYNC_INVALID_SYM_KEY  NULL

/* -------------------------------------------------------------------------
 * SHA-1 - EVP_MD_CTX based
 *
 * Context is EVP_MD_CTX *.  Callers declare:
 *   psync_sha1_ctx ctx;
 * and pass &ctx to the init/update/final macros.  psync_sha1_final frees the
 * context automatically.
 * ------------------------------------------------------------------------- */

#define PSYNC_SHA1_BLOCK_LEN     64
#define PSYNC_SHA1_DIGEST_LEN    20
#define PSYNC_SHA1_DIGEST_HEXLEN 40

typedef EVP_MD_CTX *psync_sha1_ctx;

#define psync_sha1(data, datalen, checksum) \
  do { unsigned int _psync_hl; \
       EVP_Digest((data), (datalen), (checksum), &_psync_hl, EVP_sha1(), NULL); } while(0)

#define psync_sha1_init(pctx) \
  do { *(pctx) = EVP_MD_CTX_new(); \
       EVP_DigestInit_ex(*(pctx), EVP_sha1(), NULL); } while(0)

#define psync_sha1_update(pctx, data, datalen) \
  EVP_DigestUpdate(*(pctx), (data), (datalen))

#define psync_sha1_final(checksum, pctx) \
  do { unsigned int _psync_hl; \
       EVP_DigestFinal_ex(*(pctx), (checksum), &_psync_hl); \
       EVP_MD_CTX_free(*(pctx)); *(pctx) = NULL; } while(0)

/* -------------------------------------------------------------------------
 * SHA-512 - EVP_MD_CTX based
 * ------------------------------------------------------------------------- */

#define PSYNC_SHA512_BLOCK_LEN     128
#define PSYNC_SHA512_DIGEST_LEN    64
#define PSYNC_SHA512_DIGEST_HEXLEN 128

typedef EVP_MD_CTX *psync_sha512_ctx;

#define psync_sha512(data, datalen, checksum) \
  do { unsigned int _psync_hl; \
       EVP_Digest((data), (datalen), (checksum), &_psync_hl, EVP_sha512(), NULL); } while(0)

#define psync_sha512_init(pctx) \
  do { *(pctx) = EVP_MD_CTX_new(); \
       EVP_DigestInit_ex(*(pctx), EVP_sha512(), NULL); } while(0)

#define psync_sha512_update(pctx, data, datalen) \
  EVP_DigestUpdate(*(pctx), (data), (datalen))

#define psync_sha512_final(checksum, pctx) \
  do { unsigned int _psync_hl; \
       EVP_DigestFinal_ex(*(pctx), (checksum), &_psync_hl); \
       EVP_MD_CTX_free(*(pctx)); *(pctx) = NULL; } while(0)

/* -------------------------------------------------------------------------
 * SHA-256 - EVP_MD_CTX based
 * ------------------------------------------------------------------------- */

#define PSYNC_SHA256_BLOCK_LEN     64
#define PSYNC_SHA256_DIGEST_LEN    32
#define PSYNC_SHA256_DIGEST_HEXLEN 64

typedef EVP_MD_CTX *psync_sha256_ctx;

#define psync_sha256(data, datalen, checksum) \
  do { unsigned int _psync_hl; \
       EVP_Digest((data), (datalen), (checksum), &_psync_hl, EVP_sha256(), NULL); } while(0)

#define psync_sha256_init(pctx) \
  do { *(pctx) = EVP_MD_CTX_new(); \
       EVP_DigestInit_ex(*(pctx), EVP_sha256(), NULL); } while(0)

#define psync_sha256_update(pctx, data, datalen) \
  EVP_DigestUpdate(*(pctx), (data), (datalen))

#define psync_sha256_final(checksum, pctx) \
  do { unsigned int _psync_hl; \
       EVP_DigestFinal_ex(*(pctx), (checksum), &_psync_hl); \
       EVP_MD_CTX_free(*(pctx)); *(pctx) = NULL; } while(0)

/* ── AES-256 encoder / decoder ───────────────────────────────────────────────
 * EVP_CIPHER_CTX in ECB mode (padding disabled).  OpenSSL 3.x applies AES-NI
 * internally via the EVP layer; no manual HW routing is needed.
 * EVP_CIPHER_CTX_free() zeroes the key schedule on destruction.
 * ─────────────────────────────────────────────────────────────────────────── */

typedef EVP_CIPHER_CTX *psync_aes256_encoder;
typedef EVP_CIPHER_CTX *psync_aes256_decoder;

static inline void psync_aes256_encode_block(psync_aes256_encoder enc,
    const unsigned char *src, unsigned char *dst) {
  int outl;
  EVP_EncryptUpdate(enc, dst, &outl, src, PSYNC_AES256_BLOCK_SIZE);
}
static inline void psync_aes256_decode_block(psync_aes256_decoder dec,
    const unsigned char *src, unsigned char *dst) {
  int outl;
  EVP_DecryptUpdate(dec, dst, &outl, src, PSYNC_AES256_BLOCK_SIZE);
}
static inline void psync_aes256_encode_2blocks_consec(psync_aes256_encoder enc,
    const unsigned char *src, unsigned char *dst) {
  int outl;
  EVP_EncryptUpdate(enc, dst, &outl, src, 2 * PSYNC_AES256_BLOCK_SIZE);
}
static inline void psync_aes256_decode_2blocks_consec(psync_aes256_decoder dec,
    const unsigned char *src, unsigned char *dst) {
  int outl;
  EVP_DecryptUpdate(dec, dst, &outl, src, 2 * PSYNC_AES256_BLOCK_SIZE);
}
static inline void psync_aes256_decode_4blocks_consec_xor(psync_aes256_decoder dec,
    const unsigned char *src, unsigned char *dst, unsigned char *bxor) {
  int outl;
  unsigned long i;
  EVP_DecryptUpdate(dec, dst, &outl, src, 4 * PSYNC_AES256_BLOCK_SIZE);
  for (i = 0; i < 4 * PSYNC_AES256_BLOCK_SIZE / sizeof(unsigned long); i++)
    ((unsigned long *)dst)[i] ^= ((unsigned long *)bxor)[i];
}

typedef void (*psync_ssl_debug_callback_t)(void *ctx, int level, const char *message);
void psync_ssl_set_log_threshold(int threshold);
void psync_ssl_set_debug_callback(psync_ssl_debug_callback_t cb, void *ctx);

#endif  /* _PSYNC_OPENSSL3_H */
