#if defined(__x86_64__)
typedef char bytes __attribute__((vector_size(16)));
typedef unsigned char ubytes __attribute__((vector_size(16)));
typedef short words __attribute__((vector_size(16)));
typedef float floats __attribute__((vector_size(16)));
static int calls;
__attribute__((noinline)) static bytes input(void) { calls++; return (bytes){-1,0,-1}; }
__attribute__((target("sse4.2"),noinline))
static unsigned long long crc(unsigned long long a, unsigned long long b)
{ return __builtin_ia32_crc32di(a,b); }
static unsigned reference(unsigned a, unsigned long long b)
{
 for (int i=0;i<8;i++,b>>=8) {
  a ^= (unsigned char)b;
  for (int j=0;j<8;j++) a=(a>>1)^((a&1)?0x82f63b78u:0);
 }
 return a;
}
int main(void)
{
 if (__builtin_ia32_pmovmskb128(input()) != 5 || calls != 1) return 1;
 ubytes a={0,1,127,255},b={1,0,128,254};
 ubytes r=__builtin_elementwise_sub_sat(a,b);
 if(r[0] || r[1]!=1 || r[2] || r[3]!=1) return 2;
 r=__builtin_elementwise_min(a,b);
 if(r[0] || r[1] || r[2]!=127 || r[3]!=254) return 3;
 volatile int count=16;
 words w={-1,1,32767,-32768};
 words z=__builtin_ia32_psllwi128(w,count);
 for(int i=0;i<8;i++) if(z[i]) return 4;
 count=-1;z=__builtin_ia32_psrlwi128(w,count);
 for(int i=0;i<8;i++) if(z[i]) return 5;
 union { floats f; unsigned u[4]; } x,y,out;
 x.u[0]=0x7fc00011; x.u[1]=0x80000000; x.u[2]=0; x.u[3]=0x3f800000;
 y.u[0]=0x40000000; y.u[1]=0; y.u[2]=0x80000000; y.u[3]=0x7fc00022;
 out.f=__builtin_ia32_minps(x.f,y.f);
 for(int i=0;i<4;i++) if(out.u[i]!=y.u[i]) return 6;
 out.f=__builtin_ia32_maxps(x.f,y.f);
 for(int i=0;i<4;i++) if(out.u[i]!=y.u[i]) return 7;
 unsigned ax=1,bx,cx,dx;
 __asm__("cpuid" : "+a"(ax),"=b"(bx),"=c"(cx),"=d"(dx));
 if(cx&(1u<<20)) for(unsigned i=0;i<16;i++) {
  unsigned seed=0xffffffffu-i*12345;
  unsigned long long data=0x123456789abcdef0ull+i*0x123456789ull;
  if(crc(seed,data)!=reference(seed,data)) return 8;
 }
 return 0;
}
#else
int main(void) { return 0; }
#endif
