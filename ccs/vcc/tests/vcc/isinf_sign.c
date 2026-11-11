/* Signed infinity classification is used by musl's isinf macro. */
#define CHECK(T, TAG, INF, NAN, SNAN) \
static volatile T TAG##_values[] = {0.0,-0.0,1.0,-1.0,INF,-INF,NAN,-NAN,SNAN,-SNAN}; \
static int TAG##_calls; \
static T TAG##_once(int i) { TAG##_calls++; return TAG##_values[i]; } \
static int TAG##_check(void) { \
 for(int i=0;i<10;i++) { \
  int result=__builtin_isinf_sign(TAG##_once(i)); \
  if(result!=(i==4?1:i==5?-1:0) || TAG##_calls!=i+1)return 1; \
 } return 0; }
CHECK(float,f,__builtin_inff(),__builtin_nanf("1"),__builtin_nansf("1"))
CHECK(double,d,__builtin_inf(),__builtin_nan("1"),__builtin_nans("1"))
CHECK(long double,l,__builtin_infl(),__builtin_nanl("1"),__builtin_nansl("1"))
CHECK(_Float16,h,(_Float16)__builtin_inff(),(_Float16)__builtin_nanf("1"),(_Float16)__builtin_nansf("1"))
CHECK(__bf16,b,(__bf16)__builtin_inff(),(__bf16)__builtin_nanf("1"),(__bf16)__builtin_nansf("1"))
int main(void) {
 if(f_check() || d_check() || l_check() || h_check() || b_check())return 1;
 for(unsigned i=0;i<65536;i++) {
  union {_Float16 f; unsigned short u;} h;
  union {__bf16 f; unsigned short u;} b;
  h.u=i;b.u=i;
  volatile _Float16 hv=h.f;volatile __bf16 bv=b.f;
  int sign=i&0x8000?-1:1;
  if(__builtin_isinf_sign(hv)!=((i&0x7fff)==0x7c00?sign:0))return 4;
  if(__builtin_isinf_sign(bv)!=((i&0x7fff)==0x7f80?sign:0))return 5;
 }
 union {float f;unsigned u;} f;
 union {double f;unsigned long long u;} d;
 for(int sign=0;sign<2;sign++) {
  f.u=(unsigned)sign<<31|1;d.u=(unsigned long long)sign<<63|1;
  volatile float sf=f.f;volatile double sd=d.f;
  if(__builtin_isinf_sign(sf)||__builtin_isinf_sign(sd))return 2;
 }
#if defined(__aarch64__)
 union {long double f; unsigned __int128 u;} l;
 unsigned __int128 bits[]={1,((unsigned __int128)1<<112)-1,(unsigned __int128)1<<112,
 ((unsigned __int128)0x7fff<<112)|1,((unsigned __int128)0x7fff<<112)|((unsigned __int128)1<<111)};
 for(int sign=0;sign<2;sign++)for(int i=0;i<5;i++) {
  l.u=bits[i]|((unsigned __int128)sign<<127);
  volatile long double x=l.f;
  if(__builtin_isinf_sign(x))return 3;
 }
#endif
 return 0;
}
