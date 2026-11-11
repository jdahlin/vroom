/* Driven by bench/vcc-x86-wide.py; needs host glibc and AVX-512. */
#include <immintrin.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {
 unsigned char a[64]; for(int i=0;i<64;i++) a[i]=(unsigned char)(i*7+13);
 __m512i x=_mm512_loadu_si512(a),y=_mm512_popcnt_epi64(x);
 unsigned ref=0;for(int i=0;i<64;i++)for(int j=0;j<8;j++)ref+=(a[i]>>j)&1;
 if(_mm512_reduce_add_epi64(y)!=ref) return 1;
 __m512i z=_mm512_ternarylogic_epi64(x,x,x,0x96);
 for(int i=0;i<8;i++)if(((unsigned long long*)&z)[i]!=((unsigned long long*)&x)[i])return 2;
 __m128i q=_mm512_extracti32x4_epi32(x,2);
 for(int i=0;i<16;i++)if(((unsigned char*)&q)[i]!=a[32+i])return 3;
 long n=sysconf(_SC_PAGESIZE);
 unsigned char *p=mmap(0,n*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(p==MAP_FAILED)return 4;
 for(int i=0;i<8;i++)p[n-8+i]=i+1;
 if(mprotect(p+n,n,PROT_NONE))return 5;
 z=_mm512_maskz_loadu_epi8(255,p+n-8);
 for(int i=0;i<64;i++)if(((unsigned char*)&z)[i]!=(i<8?i+1:0))return 6;
 if((_xgetbv(0)&0xe6)!=0xe6)return 7;
 munmap(p,n*2);return 0;
}
