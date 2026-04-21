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
#include <sys/types.h>

static const uint8_t __hex_lookupl[513]={
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

const char base64_table[]={
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_'
};

static const char base64_reverse_table[256]={
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -1, -1, -2, -2, -1, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -1, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, 62, -2, 62, -2, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -2, -2, -2, -1, -2, -2,
        -2,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -2, -2, -2, -2, 63,
        -2, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2
};

uint16_t const *__hex_lookup=(uint16_t *)__hex_lookupl;

unsigned char *psync_base32_encode(const unsigned char *str, size_t length, size_t *ret_length){
  static const unsigned char *table=(const unsigned char *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  unsigned char *result;
  unsigned char *p;
  uint32_t bits, buff;

  result=(unsigned char *)psync_malloc(((length+4)/5)*8+1);
  p=result;

  bits=0;
  buff=0; // don't really have to initialize this one, but a compiler that will detect that this is safe is yet to be born

  while (length){
    if (bits<5){
      buff=(buff<<8)|(*str++);
      length--;
      bits+=8;
    }
    bits-=5;
    *p++=table[0x1f&(buff>>bits)];
  }

  while (bits){
    if (bits<5){
      buff<<=(5-bits);
      bits=5;
    }
    bits-=5;
    *p++=table[0x1f&(buff>>bits)];
  }

  *ret_length=p-result;
  *p=0;
  return result;
}

unsigned char *psync_base32_decode(const unsigned char *str, size_t length, size_t *ret_length){
  unsigned char *result, *p;
  uint32_t bits, buff;
  unsigned char ch;
  result=(unsigned char *)psync_malloc((length+7)/8*5+1);
  p=result;
  bits=0;
  buff=0;
  while (length){
    ch=*str++;
    length--;
    if (ch>='A' && ch<='Z')
      ch=(ch&0x1f)-1;
    else if (ch>='2'&&ch<='7')
      ch-='2'-26;
    else{
      psync_free(result);
      return NULL;
    }
    buff=(buff<<5)+ch;
    bits+=5;
    if (bits>=8){
      bits-=8;
      *p++=buff>>bits;
    }
  }
  *p=0;
  *ret_length=p-result;
  return result;
}

unsigned char *psync_base64_encode(const unsigned char *str, size_t length, size_t *ret_length){
  const unsigned char *current = str;
  unsigned char *p;
  unsigned char *result;

  result=(unsigned char *)psync_malloc(((length+2)/3)*4+1);
  p=result;

  while(length>2){
    *p++=base64_table[current[0] >> 2];
    *p++=base64_table[((current[0] & 0x03) << 4) + (current[1] >> 4)];
    *p++=base64_table[((current[1] & 0x0f) << 2) + (current[2] >> 6)];
    *p++=base64_table[current[2] & 0x3f];
    current+=3;
    length-=3;
  }

  if (length!=0){
    *p++=base64_table[current[0] >> 2];
    if (length>1){
      *p++=base64_table[((current[0] & 0x03) << 4) + (current[1] >> 4)];
      *p++=base64_table[(current[1] & 0x0f) << 2];
    }
    else
      *p++=base64_table[(current[0] & 0x03) << 4];
  }

  *ret_length=p-result;
  *p=0;
  return result;
}

unsigned char *psync_base64_decode(const unsigned char *str, size_t length, size_t *ret_length){
  const unsigned char *current = str;
  unsigned char *result;
  size_t i=0, j=0;
  ssize_t ch;

  result=(unsigned char *)psync_malloc((length+3)/4*3+1);

  while (length-- > 0){
    ch=base64_reverse_table[*current++];
    if (ch==-1)
     continue;
    else if (ch==-2) {
       psync_free(result);
      return NULL;
    }
    switch(i%4) {
      case 0:
        result[j]=ch<<2;
        break;
      case 1:
        result[j++]|=ch>>4;
        result[j]=(ch&0x0f)<<4;
        break;
      case 2:
        result[j++]|=ch>>2;
        result[j]=(ch&0x03)<<6;
        break;
      case 3:
        result[j++]|=ch;
        break;
    }
    i++;
  }
  *ret_length=j;
  result[j]=0;
  return result;
}
