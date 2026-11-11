#define CHECK_MASK(m) if (!!__builtin_isfpclass(x, m) != !!(expected[i] & (m))) return 1
#define CHECK(T, suffix, tiny) \
static int check_##suffix(void) { \
 volatile T values[] = {__builtin_nans##suffix(""),__builtin_nan##suffix(""), \
 -__builtin_huge_val##suffix(),-1.0,-tiny,-0.0,0.0,tiny,1.0,__builtin_huge_val##suffix()}; \
 int expected[] = {1,2,4,8,16,32,64,128,256,512}; \
 for (int i=0;i<10;i++) { \
  T x=values[i]; \
  if (!!__builtin_issubnormal(x) != (i==4 || i==7) || !!__builtin_iszero(x) != (i==5 || i==6)) return 3; \
  CHECK_MASK(0); CHECK_MASK(1); CHECK_MASK(2); CHECK_MASK(4); CHECK_MASK(8); \
  CHECK_MASK(16); CHECK_MASK(32); CHECK_MASK(64); CHECK_MASK(128); \
  CHECK_MASK(256); CHECK_MASK(512); CHECK_MASK(1023); CHECK_MASK(3); CHECK_MASK(504); \
  int j=i; if (!__builtin_isfpclass(values[j++],1023) || j!=i+1) return 2; \
 } return 0; \
}
CHECK(float,f,__FLT_DENORM_MIN__)
CHECK(double,,__DBL_DENORM_MIN__)
CHECK(long double,l,__LDBL_DENORM_MIN__)
static int constant_class = __builtin_isfpclass(1.0,256);
static int constant_subnormal = __builtin_isfpclass(__DBL_DENORM_MIN__,128);
int main(void) { return !constant_class || !constant_subnormal || check_f() || check_() || check_l(); }
