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

#include "pcore.h"
#include <stdarg.h>
#include <stdio.h>

static char normalize_table[256];

char *psync_strdup(const char *str){
  size_t len;
  len=strlen(str)+1;
  return (char *)memcpy(psync_new_cnt(char, len), str, len);
}

char *psync_strnormalize_filename(const char *str){
  size_t len, i;
  char *ptr;
  len=strlen(str)+1;
  ptr=psync_new_cnt(char, len);
  for (i=0; i<len; i++)
    ptr[i]=normalize_table[(unsigned char)str[i]];
  return ptr;
}

char *psync_strndup(const char *str, size_t len){
  char *ptr;
  ptr=(char *)memcpy(psync_new_cnt(char, len+1), str, len);
  ptr[len]=0;
  return ptr;
}

char *psync_strcat(const char *str, ...){
  size_t i, size, len;
  const char *strs[64];
  size_t lengths[64];
  const char *ptr;
  char *ptr2, *ptr3;
  va_list ap;
  va_start(ap, str);

  strs[0]=str;
  len=strlen(str);
  lengths[0]=len;
  size=len+1;
  i=1;

  while ((ptr=va_arg(ap, const char *))){
    if (unlikely(i>=ARRAY_SIZE(strs))){
      debug(D_BUG, "psync_strcat called with more than %u arguments", (unsigned)ARRAY_SIZE(strs));
      abort();
    }
    len=strlen(ptr);
    lengths[i]=len;
    strs[i++]=ptr;
    size+=len;
  }

  va_end(ap);
  ptr2=ptr3=(char *)psync_malloc(size);

  for (size=0; size<i; size++){
    memcpy(ptr2, strs[size], lengths[size]);
    ptr2+=lengths[size];
  }

  *ptr2=0;

  return ptr3;
}

int psync_slprintf(char *str, size_t size, const char *format, ...){
  va_list ap;
  int ret;
  va_start(ap, format);
  ret=vsnprintf(str, size, format, ap);
  va_end(ap);
  if (unlikely_log(ret>=size))
    str[size-1]=0;
  return ret;
}

int psync_is_valid_utf8(const char *str){
  static const int8_t trailing[]={
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, -1, -1, -1, -1, -1, -1, -1, -1
  };
  int8_t t;
  while (*str) {
    t=trailing[(unsigned char)*str++];
    if (unlikely(t)){
      if (t<0)
        return 0;
      while (t--)
        if ((((unsigned char)*str++)&0xc0)!=0x80)
          return 0;
    }
  }
  return 1;
}

void psync_libs_init(){
  psync_uint_t i;
  for (i=0; i<256; i++)
    normalize_table[i]=i;
  normalize_table[':']='_';
  normalize_table['/']='_';
  normalize_table['\\']='_';
}

int psync_match_pattern(const char *name, const char *pattern, size_t plen){
  size_t i;
  for (i=0; i<plen; i++){
    if (pattern[i]=='*'){
      name+=i;
      while (1){
        if (++i==plen)
          return 1;
        switch (pattern[i]){
          case '?':
            if (!*name++)
              return 0;
          case '*':
            break;
          default:
            name=strchr(name, pattern[i]);
            pattern+=i+1;
            plen-=i+1;
            while (name){
              name++;
              if (psync_match_pattern(name, pattern, plen))
                return 1;
              name=strchr(name, *(pattern-1));
            }
            return 0;
        }
      }
    }
    else if (!name[i] || (pattern[i]!=name[i] && pattern[i]!='?'))
      return 0;
  }
  return name[i]==0;
}

uint64_t psync_ato64(const char *str){
  uint64_t n=0;
  while (*str>='0' && *str<='9'){
    unsigned digit=(*str++)-'0';
    if (unlikely(n>(UINT64_MAX-digit)/10)){
      debug(D_WARNING, "psync_ato64 overflow on input starting with '%.20s'", str-1);
      return UINT64_MAX;
    }
    n=n*10+digit;
  }
  return n;
}

uint32_t psync_ato32(const char *str){
  uint32_t n=0;
  while (*str>='0' && *str<='9'){
    unsigned digit=(*str++)-'0';
    if (unlikely(n>(UINT32_MAX-digit)/10)){
      debug(D_WARNING, "psync_ato32 overflow on input starting with '%.20s'", str-1);
      return UINT32_MAX;
    }
    n=n*10+digit;
  }
  return n;
}
