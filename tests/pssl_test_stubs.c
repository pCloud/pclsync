/* Stub implementations for non-crypto dependencies pulled in by pssl-*.c
 * and pssl.c at link time during unit testing.
 *
 * Compiled without a provider define so it cannot include pssl.h.
 * Uses only standard C types. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* PSYNC_LHASH_DIGEST_LEN = PSYNC_SHA512_DIGEST_LEN = 64 */
#define STUBS_SEED_LEN 64

/* ── Core allocator (psynclib.c stubs) ────────────────────────────────────── */
void *psync_malloc(size_t size)            { return malloc(size); }
void  psync_free(void *ptr)               { free(ptr); }
void *psync_realloc(void *ptr, size_t sz) { return realloc(ptr, sz); }

/* ── Locked allocator (pmemlock.c is stubbed to plain malloc) ─────────────── */
void *psync_locked_malloc(size_t size) { return malloc(size); }
void  psync_locked_free(void *ptr)     { free(ptr); }

/* ── Session cache (pcache.h) — no caching in tests ──────────────────────── */
void *psync_cache_get(const char *key) { (void)key; return NULL; }
void  psync_cache_add(const char *key, void *val, time_t ttl,
                      void (*dtor)(void *), uint32_t maxitems) {
  (void)key; (void)ttl; (void)maxitems;
  if (dtor) dtor(val);
}

/* ── Entropy (pcompat.h) — /dev/urandom is sufficient for tests ───────────── */
void psync_get_random_seed(unsigned char *seed, const void *addent,
                           size_t aelen, int fast) {
  (void)addent; (void)aelen; (void)fast;
  FILE *f = fopen("/dev/urandom", "rb");
  if (f) { fread(seed, 1, STUBS_SEED_LEN, f); fclose(f); }
}

/* ── Base64 encoder (plibs.c stub) ───────────────────────────────────────────
 * Used by psync_ssl_derive_password_from_passphrase in pssl-openssl3.c.
 * Self-contained RFC 4648 base64 implementation. */
/* ── Hex-lookup table (plibs.c / __hex_lookup) ───────────────────────────────
 * Used by the psync_binhex() macro in plibs.h.  The table maps each byte
 * value 0x00..0xFF to a two-character hex string stored as uint16_t. */
static const uint8_t __hex_lookupl[513] = {
  "000102030405060708090a0b0c0d0e0f"
  "101112131415161718191a1b1c1d1e1f"
  "202122232425262728292a2b2c2d2e2f"
  "303132333435363738393a3b3c3d3e3f"
  "404142434445464748494a4b4c4d4e4f"
  "505152535455565758595a5b5c5d5e5f"
  "606162636465666768696a6b6c6d6e6f"
  "707172737475767778797a7b7c7d7e7f"
  "808182838485868788898a8b8c8d8e8f"
  "909192939495969798999a9b9c9d9e9f"
  "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
  "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
  "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
  "d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
  "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
  "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"
};
uint16_t const *__hex_lookup = (uint16_t *)__hex_lookupl;

/* ── Base64 encoder (plibs.c stub) ───────────────────────────────────────────
 * Used by psync_ssl_derive_password_from_passphrase in pssl-openssl3.c.
 * Self-contained RFC 4648 base64 implementation. */
unsigned char *psync_base64_encode(const unsigned char *in, size_t len,
                                   size_t *ret_length) {
  static const char enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t olen = ((len + 2) / 3) * 4;
  unsigned char *out = malloc(olen + 1);
  size_t i = 0, j = 0;
  if (!out) { if (ret_length) *ret_length = 0; return NULL; }
  while (i < len) {
    unsigned int b0 = in[i++];
    unsigned int b1 = (i < len) ? in[i++] : 0;
    unsigned int b2 = (i < len) ? in[i++] : 0;
    unsigned int v  = (b0 << 16) | (b1 << 8) | b2;
    out[j++] = (unsigned char)enc[(v >> 18) & 0x3F];
    out[j++] = (unsigned char)enc[(v >> 12) & 0x3F];
    out[j++] = (unsigned char)enc[(v >>  6) & 0x3F];
    out[j++] = (unsigned char)enc[(v >>  0) & 0x3F];
  }
  if (len % 3 >= 1) out[olen - 1] = '=';
  if (len % 3 == 1) out[olen - 2] = '=';
  out[olen] = '\0';
  if (ret_length) *ret_length = olen;
  return out;
}
