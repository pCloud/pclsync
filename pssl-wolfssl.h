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

#ifndef _PSYNC_WOLFSSL_H
#define _PSYNC_WOLFSSL_H

#include "plibs.h"

#include <wolfssl/wolfcrypt/settings.h>

#include <wolfssl/wolfcrypt/sha.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/sha512.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/aes.h>

#define PSYNC_INVALID_RSA NULL
#define PSYNC_INVALID_SYM_KEY NULL

#define PSYNC_SHA1_BLOCK_LEN 64
#define PSYNC_SHA1_DIGEST_LEN 20
#define PSYNC_SHA1_DIGEST_HEXLEN 40
#define psync_sha1_ctx wc_Sha
#define psync_sha1(data, datalen, checksum) wc_ShaHash((const byte*)data, (word32)datalen, (byte*)checksum)
#define psync_sha1_init(pctx) wc_InitSha(pctx)
#define psync_sha1_update(pctx, data, datalen) wc_ShaUpdate(pctx, (const unsigned char *)data, datalen)
#define psync_sha1_final(checksum, pctx) wc_ShaFinal(pctx, checksum)

#define PSYNC_SHA256_BLOCK_LEN 64
#define PSYNC_SHA256_DIGEST_LEN 32
#define PSYNC_SHA256_DIGEST_HEXLEN 64
#define psync_sha256_ctx wc_Sha256
#define psync_sha256(data, datalen, checksum) wc_Sha256Hash((const byte*)data, (word32)datalen, (byte*)checksum)
#define psync_sha256_init(pctx) wc_InitSha256(pctx)
#define psync_sha256_update(pctx, data, datalen) wc_Sha256Update(pctx, (const unsigned char *)data, datalen)
#define psync_sha256_final(checksum, pctx) wc_Sha256Final(pctx, checksum)

#define PSYNC_SHA512_BLOCK_LEN 128
#define PSYNC_SHA512_DIGEST_LEN 64
#define PSYNC_SHA512_DIGEST_HEXLEN 128
#define psync_sha512_ctx wc_Sha512
#define psync_sha512(data, datalen, checksum) wc_Sha512Hash((const byte*)data, (word32)datalen, (byte*)checksum)
#define psync_sha512_init(pctx) wc_InitSha512(pctx)
#define psync_sha512_update(pctx, data, datalen) wc_Sha512Update(pctx, (const unsigned char *)data, datalen)
#define psync_sha512_final(checksum, pctx) wc_Sha512Final(pctx, checksum)

typedef struct RsaKey *psync_rsa_t;
typedef struct RsaKey *psync_rsa_publickey_t;
typedef struct RsaKey *psync_rsa_privatekey_t;

typedef struct {
  size_t keylen;
  unsigned char key[];
} psync_symmetric_key_struct_t, *psync_symmetric_key_t;

typedef struct Aes *psync_aes256_encoder;
typedef struct Aes *psync_aes256_decoder;

typedef void (*psync_ssl_debug_callback_t)(void *ctx, int level, const char *message);
void psync_ssl_set_log_threshold(int threshold);
void psync_ssl_set_debug_callback(psync_ssl_debug_callback_t cb, void *ctx);

#if defined(__GNUC__) && (defined(__amd64__) || defined(__x86_64__) || defined(__i386__))
#define PSYNC_AES_HW
#define PSYNC_AES_HW_GCC

extern int psync_ssl_hw_aes;

void psync_aes256_encode_block_hw(psync_aes256_encoder enc, const unsigned char *src, unsigned char *dst);
void psync_aes256_decode_block_hw(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst);
void psync_aes256_encode_2blocks_consec_hw(psync_aes256_encoder enc, const unsigned char *src, unsigned char *dst);
void psync_aes256_decode_2blocks_consec_hw(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst);
void psync_aes256_decode_4blocks_consec_xor_hw(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst, unsigned char *bxor);
void psync_aes256_decode_4blocks_consec_xor_sw(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst, unsigned char *bxor);

static inline void psync_aes256_encode_block(psync_aes256_encoder enc, const unsigned char *src, unsigned char *dst){
  if (likely(psync_ssl_hw_aes))
    psync_aes256_encode_block_hw(enc, src, dst);
  else
    wc_AesEncryptDirect(enc, dst, src);
}

static inline void psync_aes256_decode_block(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst){
  if (likely(psync_ssl_hw_aes))
    psync_aes256_decode_block_hw(enc, src, dst);
  else
    wc_AesDecryptDirect(enc, dst, src);
}

static inline void psync_aes256_encode_2blocks_consec(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst){
  if (likely(psync_ssl_hw_aes))
    psync_aes256_encode_2blocks_consec_hw(enc, src, dst);
  else{
    wc_AesEncryptDirect(enc, dst, src);
    wc_AesEncryptDirect(enc, dst+PSYNC_AES256_BLOCK_SIZE, src+PSYNC_AES256_BLOCK_SIZE);
  }
}

static inline void psync_aes256_decode_2blocks_consec(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst){
  if (likely(psync_ssl_hw_aes))
    psync_aes256_decode_2blocks_consec_hw(enc, src, dst);
  else{
    wc_AesDecryptDirect(enc, dst, src);
    wc_AesDecryptDirect(enc, dst+PSYNC_AES256_BLOCK_SIZE, src+PSYNC_AES256_BLOCK_SIZE);
  }
}

static inline void psync_aes256_decode_4blocks_consec_xor(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst, unsigned char *bxor){
  if (psync_ssl_hw_aes)
    psync_aes256_decode_4blocks_consec_xor_hw(enc, src, dst, bxor);
  else
    psync_aes256_decode_4blocks_consec_xor_sw(enc, src, dst, bxor);
}

#else

static inline void psync_aes256_encode_block(psync_aes256_encoder enc, const unsigned char *src, unsigned char *dst){
  wc_AesEncryptDirect(enc, dst, src);
}

static inline void psync_aes256_decode_block(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst){
  wc_AesDecryptDirect(enc, dst, src);
}

static inline void psync_aes256_encode_2blocks_consec(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst){
  wc_AesEncryptDirect(enc, dst, src);
  wc_AesEncryptDirect(enc, dst+PSYNC_AES256_BLOCK_SIZE, src+PSYNC_AES256_BLOCK_SIZE);
}

static inline void psync_aes256_decode_2blocks_consec(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst){
  wc_AesDecryptDirect(enc, dst, src);
  wc_AesDecryptDirect(enc, dst+PSYNC_AES256_BLOCK_SIZE, src+PSYNC_AES256_BLOCK_SIZE);
}

static inline void psync_aes256_decode_4blocks_consec_xor(psync_aes256_decoder enc, const unsigned char *src, unsigned char *dst, unsigned char *bxor){
  unsigned long i;
  wc_AesDecryptDirect(enc, dst, src);
  wc_AesDecryptDirect(enc, dst+PSYNC_AES256_BLOCK_SIZE, src+PSYNC_AES256_BLOCK_SIZE);
  wc_AesDecryptDirect(enc, dst+PSYNC_AES256_BLOCK_SIZE*2, src+PSYNC_AES256_BLOCK_SIZE*2);
  wc_AesDecryptDirect(enc, dst+PSYNC_AES256_BLOCK_SIZE*3, src+PSYNC_AES256_BLOCK_SIZE*3);
  for (i=0; i<PSYNC_AES256_BLOCK_SIZE*4/sizeof(unsigned long); i++)
    ((unsigned long *)dst)[i]^=((unsigned long *)bxor)[i];
}

#endif

#endif