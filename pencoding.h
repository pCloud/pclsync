/* Copyright (c) 2013-2014 Anton Titov.
 * Copyright (c) 2013-2014 pCloud Ltd.
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

#ifndef _PSYNC_ENCODING_H
#define _PSYNC_ENCODING_H

#include "pcompiler.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern uint16_t const *__hex_lookup;
extern const char base64_table[];

#define psync_binhex(dst, src, cnt) \
  do {\
    size_t it__;\
    size_t cnt__=(cnt);\
    const uint8_t *src__=(const uint8_t *)(src);\
    uint16_t *dst__=(uint16_t *)(dst);\
    for (it__=0; it__<cnt__; it__++)\
      dst__[it__]=__hex_lookup[src__[it__]];\
  } while (0)

unsigned char *psync_base32_encode(const unsigned char *str, size_t length, size_t *ret_length);
unsigned char *psync_base32_decode(const unsigned char *str, size_t length, size_t *ret_length);

unsigned char *psync_base64_encode(const unsigned char *str, size_t length, size_t *ret_length);
unsigned char *psync_base64_decode(const unsigned char *str, size_t length, size_t *ret_length);

/* needs 12 characters of buffer space on top of the length of the prefix */
static inline void psync_get_string_id(char *dst, const char *prefix, uint64_t id){
  size_t plen;
  plen=strlen(prefix);
  dst=(char *)memcpy(dst, prefix, plen)+plen;
  do {
    *dst++=base64_table[id%64];
    id/=64;
  } while (id);
  *dst=0;
}

/* needs 24 characters of buffer space on top of the length of the prefix */
static inline void psync_get_string_id2(char *dst, const char *prefix, uint64_t id1, uint64_t id2){
  size_t plen;
  plen=strlen(prefix);
  dst=(char *)memcpy(dst, prefix, plen)+plen;
  do {
    *dst++=base64_table[id1%64];
    id1/=64;
  } while (id1);
  *dst++='.';
  do {
    *dst++=base64_table[id2%64];
    id2/=64;
  } while (id2);
  *dst=0;
}

#endif
