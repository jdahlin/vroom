/* More live rotating values than GP registers, plus bit-exact scalar FP signs. */
#define NI __attribute__((noinline))
NI unsigned long rotate(unsigned n,const unsigned long *p) {
#define V(N) unsigned long a##N=p[N];
 V(0) V(1) V(2) V(3) V(4) V(5) V(6) V(7)
 V(8) V(9) V(10) V(11) V(12) V(13) V(14) V(15)
#undef V
 while(n--) {
  unsigned long t=a0;
  a0=a1;a1=a2;a2=a3;a3=a4;a4=a5;a5=a6;a6=a7;a7=a8;
  a8=a9;a9=a10;a10=a11;a11=a12;a12=a13;a13=a14;a14=a15;a15=t;
 }
 return a0^(a1*3)^(a2*5)^(a3*7)^(a4*9)^(a5*11)^(a6*13)^(a7*15)^
  (a8*17)^(a9*19)^(a10*21)^(a11*23)^(a12*25)^(a13*27)^(a14*29)^(a15*31);
}
NI double negd(double x) {return -x;}
NI double absd(double x) {return __builtin_fabs(x);}
NI float negf(float x) {return -x;}
NI float absf(float x) {return __builtin_fabsf(x);}
int main(void) {
 unsigned long p[16];for(unsigned i=0;i<16;i++)p[i]=i*123456789UL+987654321UL;
 for(unsigned n=0;n<40;n++) {
  unsigned long expected=0;for(unsigned j=0;j<16;j++)expected^=p[(j+n)%16]*(2*j+1);
  if(rotate(n,p)!=expected)return 1;
 }
 unsigned long long bits[]={0,1,0x7ff0000000000000ULL,0x7ff0000000001234ULL,0x7ff8000000001234ULL};
 for(unsigned i=0;i<5;i++)for(unsigned sign=0;sign<2;sign++) {
  union {unsigned long long u;double d;} x,y;x.u=bits[i]|((unsigned long long)sign<<63);
  y.d=negd(x.d);if(y.u!=(x.u^0x8000000000000000ULL))return 2;
  y.d=absd(x.d);if(y.u!=(x.u&0x7fffffffffffffffULL))return 3;
 }
 unsigned fbits[]={0,1,0x7f800000u,0x7f801234u,0x7fc01234u};
 for(unsigned i=0;i<5;i++)for(unsigned sign=0;sign<2;sign++) {
  union {unsigned u;float f;} x,y;x.u=fbits[i]|(sign<<31);
  y.f=negf(x.f);if(y.u!=(x.u^0x80000000u))return 4;
  y.f=absf(x.f);if(y.u!=(x.u&0x7fffffffu))return 5;
 }
 return 0;
}
