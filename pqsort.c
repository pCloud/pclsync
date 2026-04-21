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

#define rot(x,k) (((x)<<(k))|((x)>>(32-(k))))
static uint32_t pq_rnd() {
  static uint32_t a=0x95ae3d25, b=0xe225d755, c=0xc63a2ae7, d=0xe4556265;
  uint32_t e=a-rot(b, 27);
  a=b^rot(c, 17);
  b=c+d;
  c=d+e;
  d=e+a;
  return d;
}

#define QSORT_TRESH  8
#define QSORT_MTR   64
#define QSORT_REC_M (16*1024)

static inline void sw2(unsigned char **a, unsigned char **b) {
  unsigned char *tmp=*a;
  *a=*b;
  *b=tmp;
}

static unsigned char *med5(unsigned char *a, unsigned char *b, unsigned char *c, unsigned char *d, unsigned char *e,
                           int (*compar)(const void *, const void *)) {
  if (compar(b, a)<0)
    sw2(&a, &b);
  if (compar(d, c)<0)
    sw2(&c, &d);
  if (compar(a, c)<0) {
    a=e;
    if (compar(b, a)<0)
      sw2(&a, &b);
  } else {
    c=e;
    if (compar(d, c)<0)
      sw2(&c, &d);
  }
  if (compar(a, c)<0)
    a=b;
  else
    c=d;
  if (compar(a, c)<0)
    return a;
  else
    return c;
}

static unsigned char *pq_choose_part(unsigned char *base, size_t cnt, size_t size, int (*compar)(const void *, const void *)) {
  if (cnt>=QSORT_REC_M) {
    cnt/=5;
    return med5(pq_choose_part(base, cnt, size, compar),
                pq_choose_part(base+cnt*size, cnt, size, compar),
                pq_choose_part(base+cnt*size*2, cnt, size, compar),
                pq_choose_part(base+cnt*size*3, cnt, size, compar),
                pq_choose_part(base+cnt*size*4, cnt, size, compar),
                compar);
  } else {
    return med5(base+(pq_rnd()%cnt)*size, base+(pq_rnd()%cnt)*size, base+(pq_rnd()%cnt)*size, base+(pq_rnd()%cnt)*size, base+(pq_rnd()%cnt)*size, compar);
  }
}

static inline void pqsswap(unsigned char *a, unsigned char *b, size_t size) {
  unsigned char tmp;
  do {
    tmp=*a;
    *a++=*b;
    *b++=tmp;
  } while (--size);
}

static inline void pqsswap32(unsigned char *a, unsigned char *b, size_t size) {
  uint32_t tmp;
  do {
    tmp=*(uint32_t *)a;
    *(uint32_t *)a=*(uint32_t *)b;
    *(uint32_t *)b=tmp;
    a+=sizeof(uint32_t);
    b+=sizeof(uint32_t);
  } while (--size);
}


typedef struct {
  unsigned char *lo;
  unsigned char *hi;
} psq_stack_t;

void psync_pqsort(void *base, size_t cnt, size_t sort_first, size_t size, int (*compar)(const void *, const void *)) {
  psq_stack_t stack[sizeof(size_t)*8];
  psq_stack_t *top;
  unsigned char *lo, *hi, *mid, *l, *r, *sf;
  size_t tresh, n, u32size;
  tresh=QSORT_TRESH*size;
  sf=(unsigned char *)base+sort_first*size;
  if (size%sizeof(uint32_t)==0 && (uintptr_t)base%sizeof(uint32_t)==0)
    u32size=size/sizeof(uint32_t);
  else
    u32size=0;
  if (cnt>QSORT_TRESH) {
    top=stack+1;
    lo=(unsigned char *)base;
    hi=lo+(cnt-1)*size;
    do {
      n=(hi-lo)/size;
      if (n<=QSORT_MTR) {
        mid=lo+(n>>1)*size;
        if (compar(mid, lo)<0)
          pqsswap(mid, lo, size);
        if (compar(hi, mid)<0) {
          pqsswap(mid, hi, size);
          if (compar(mid, lo)<0)
            pqsswap(mid, lo, size);
        }
        // we already sure *hi and *lo are good, so they will be skipped without checking
        l=lo;
        r=hi;
      } else {
        mid=pq_choose_part(lo, n, size, compar);
        l=lo-size;
        r=hi+size;
      }
      if (u32size) {
        do {
          do {
            l+=size;
          } while (compar(l, mid)<0);
          do {
            r-=size;
          } while (compar(mid, r)<0);
          if (l>=r)
            break;
          pqsswap32(l, r, u32size);
          if (mid==l) {
            mid=r;
            r+=size;
          } else if (mid==r) {
            mid=l;
            l-=size;
          }
        } while (1);
      } else {
        do {
          do {
            l+=size;
          } while (compar(l, mid)<0);
          do {
            r-=size;
          } while (compar(mid, r)<0);
          if (l>=r)
            break;
          pqsswap(l, r, size);
          if (mid==l) {
            mid=r;
            r+=size;
          } else if (mid==r) {
            mid=l;
            l-=size;
          }
        } while (1);
      }
      if (hi-mid<=tresh || mid>=sf) {
        if (mid-lo<=tresh) {
          top--;
          lo=top->lo;
          hi=top->hi;
        } else {
          hi=mid-size;
        }
      } else if (mid-lo<=tresh) {
        lo=mid+size;
      } else if (hi-mid<mid-lo) {
        top->lo=lo;
        top->hi=mid-size;
        top++;
        lo=mid+size;
      } else {
        top->lo=mid+size;
        top->hi=hi;
        top++;
        hi=mid-size;
      }
    } while (top!=stack);
  } else if (cnt<=1) {
    return;
  }
  lo=(unsigned char *)base;
  hi=lo+(cnt-1)*size;
  sf+=size*QSORT_TRESH;
  if (sf<hi)
    hi=sf;
  r=lo+QSORT_TRESH*size+4;
  if (r>hi)
    r=hi;
  for (l=lo+size; l<=r; l+=size)
    if (compar(l, lo)<0)
      lo=l;
  pqsswap((unsigned char *)base, lo, size);
  l=(unsigned char *)base+size;
  hi-=size;
  while (l<=hi) {
    lo=l;
    l+=size;
    while (compar(l, lo)<0)
      lo-=size;
    lo+=size;
    if (lo!=l) {
      unsigned char *t=l+size;
      if (u32size) {
        while ((t-=sizeof(uint32_t))>=l) {
          uint32_t tmp=*(uint32_t *)t;
          for (r=mid=t; (mid-=size)>=lo; r=mid)
            *(uint32_t *)r=*(uint32_t *)mid;
          *(uint32_t *)r=tmp;
        }
      } else {
        while (--t>=l) {
          unsigned char tmp=*t;
          for (r=mid=t; (mid-=size)>=lo; r=mid)
            *r=*mid;
          *r=tmp;
        }
      }
    }
  }
}

void psync_qpartition(void *base, size_t cnt, size_t sort_first, size_t size, int (*compar)(const void *, const void *)) {
  unsigned char *lo, *hi, *mid, *l, *r, *sf;
  size_t n, u32size;
  sf=(unsigned char *)base+sort_first*size;
  if (size%sizeof(uint32_t)==0 && (uintptr_t)base%sizeof(uint32_t)==0)
    u32size=size/sizeof(uint32_t);
  else
    u32size=0;
  if (cnt<=1) // otherwise cnt-1 will underflow
    return;
  lo=(unsigned char *)base;
  hi=lo+(cnt-1)*size;
  while (1) {
    n=(hi-lo)/size;
    if (n<=QSORT_MTR) {
      mid=lo+(n>>1)*size;
      if (compar(mid, lo)<0)
        pqsswap(mid, lo, size);
      if (compar(hi, mid)<0) {
        pqsswap(mid, hi, size);
        if (compar(mid, lo)<0)
          pqsswap(mid, lo, size);
      }
      // we already sure *hi and *lo are good, so they will be skipped without checking
      if (n<=2) // when n is 2, we have 3 elements
        return;
      l=lo;
      r=hi;
    } else {
      mid=pq_choose_part(lo, n, size, compar);
      l=lo-size;
      r=hi+size;
    }
    if (u32size) {
      do {
        do {
          l+=size;
        } while (compar(l, mid)<0);
        do {
          r-=size;
        } while (compar(mid, r)<0);
        if (l>=r)
          break;
        pqsswap32(l, r, u32size);
        if (mid==l) {
          mid=r;
          r+=size;
        } else if (mid==r) {
          mid=l;
          l-=size;
        }
      } while (1);
    } else {
      do {
        do {
          l+=size;
        } while (compar(l, mid)<0);
        do {
          r-=size;
        } while (compar(mid, r)<0);
        if (l>=r)
          break;
        pqsswap(l, r, size);
        if (mid==l) {
          mid=r;
          r+=size;
        } else if (mid==r) {
          mid=l;
          l-=size;
        }
      } while (1);
    }
    if (mid<sf)
      lo=mid+size;
    else if (mid>sf)
      hi=mid-size;
    else
      return;
  }
}
