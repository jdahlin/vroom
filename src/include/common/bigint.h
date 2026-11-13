#ifndef VCC_LIB_BIGINT_H
#define VCC_LIB_BIGINT_H
/* Fixed-width LE32 arithmetic. Callers own storage; no global mutable state. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
static inline bool vbi_nz(const uint32_t *a, size_t n) { uint32_t x=0; for(size_t i=0;i<n;i++) x|=a[i]; return x!=0; }
static inline void vbi_norm(uint32_t *a,size_t n,unsigned bits,bool sign) {
 size_t last=(bits-1)/32; unsigned used=(bits-1)%32+1; bool neg=sign&&((a[last]>>(used-1))&1);
 if(used<32) a[last]=(a[last]&((1u<<used)-1))|(neg?~((1u<<used)-1):0);
 for(size_t i=last+1;i<n;i++) a[i]=neg?UINT32_MAX:0;
}
static inline void vbi_copy(uint32_t*d,size_t n,const uint32_t*a,size_t m,bool neg) { for(size_t i=0;i<n;i++)d[i]=i<m?a[i]:neg?UINT32_MAX:0; }
static inline int vbi_cmp(const uint32_t*a,const uint32_t*b,size_t n) { while(n--)if(a[n]!=b[n])return a[n]>b[n]?1:-1;return 0; }
static inline void vbi_neg(uint32_t*d,const uint32_t*a,size_t n) { uint64_t c=1;for(size_t i=0;i<n;i++){c+=(uint64_t)(uint32_t)~a[i];d[i]=(uint32_t)c;c>>=32;} }
static inline void vbi_add(uint32_t*d,const uint32_t*a,const uint32_t*b,size_t n) { uint64_t c=0;for(size_t i=0;i<n;i++){c+=(uint64_t)a[i]+b[i];d[i]=(uint32_t)c;c>>=32;} }
static inline void vbi_sub(uint32_t*d,const uint32_t*a,const uint32_t*b,size_t n) { uint64_t borrow=0;for(size_t i=0;i<n;i++){uint64_t r=(uint64_t)a[i]-b[i]-borrow;borrow=(uint64_t)a[i]<(uint64_t)b[i]+borrow;d[i]=(uint32_t)r;} }
static inline void vbi_mul(uint32_t*d,const uint32_t*a,const uint32_t*b,size_t n) {
 memset(d,0,n*4);for(size_t i=0;i<n;i++){uint64_t carry=0;for(size_t j=0;j<n-i;j++){uint64_t v=(uint64_t)a[i]*b[j]+d[i+j]+carry;d[i+j]=(uint32_t)v;carry=v>>32;}}
}
static inline void vbi_shift(uint32_t*d,const uint32_t*a,size_t n,uint64_t shift,bool right,bool neg) {
 size_t word=(size_t)(shift/32);unsigned bit=(unsigned)(shift%32);uint32_t fill=neg?UINT32_MAX:0;
 for(size_t i=0;i<n;i++) {
  if(right){size_t k=i+word;uint32_t lo=k<n?a[k]:fill,hi=k+1<n?a[k+1]:fill;d[i]=bit?(lo>>bit)|(hi<<(32-bit)):lo;}
  else{uint32_t lo=i>=word?a[i-word]:0,hi=i>word?a[i-word-1]:0;d[i]=bit?(lo<<bit)|(hi>>(32-bit)):lo;}
 }
}
/* Unsigned restoring division; r needs n+1 words and scratch divisor n+1. */
static inline void vbi_div(uint32_t*q,uint32_t*r,const uint32_t*a,const uint32_t*b,size_t n,uint32_t*divisor) {
 memset(q,0,n*4);memset(r,0,(n+1)*4);memcpy(divisor,b,n*4);divisor[n]=0;
 for(size_t bit=n*32;bit--;){uint32_t c=(a[bit/32]>>(bit%32))&1;for(size_t i=0;i<=n;i++){uint32_t next=r[i]>>31;r[i]=(r[i]<<1)|c;c=next;}
  if(vbi_cmp(r,divisor,n+1)>=0){vbi_sub(r,r,divisor,n+1);q[bit/32]|=1u<<(bit%32);}}
}
#endif
