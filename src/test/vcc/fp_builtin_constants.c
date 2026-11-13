/* Floating builtins fold without dropping side effects or NaN sign/payload. */
#define ROW(T) { __builtin_isgreater((T)2,(T)1), __builtin_isgreaterequal((T)-0.,(T)0.), \
 __builtin_isless((T)-2,(T)-1), __builtin_islessequal((T)0.,(T)-0.), \
 __builtin_islessgreater((T)1,(T)1), __builtin_isunordered((T)1,(T)2) }
static const int normal[3][6] = {ROW(float),ROW(double),ROW(long double)};
#define NANROW(N) { __builtin_isgreater(N,1.), __builtin_isgreaterequal(1.,N), \
 __builtin_isless(N,1.), __builtin_islessequal(1.,N), \
 __builtin_islessgreater(N,1.), __builtin_isunordered(1.,N) }
static const int nans[2][6] = {NANROW(__builtin_nan("0x123")),NANROW(__builtin_nansl("0x123"))};
static const int infs[] = {__builtin_isinf_sign(__builtin_inff()),__builtin_isinf_sign(-__builtin_inf()),
 __builtin_isinf_sign(-__builtin_infl()),__builtin_isinf_sign(-0.L),__builtin_isinf_sign(__builtin_nanl(""))};
static const int precise = __builtin_isgreater(0x1.000000000000001p0L,1.L);
static const int promoted = __builtin_isgreater(16777217,16777216.f);
static const long double signzero = __builtin_copysignl(0.L,-1.L);
static const long double magnitude = __builtin_fabsl(-0x1.000000000000001p0L);
static const double payload = __builtin_copysign(__builtin_nans("0x123"),-0.);
static const float fpayload = __builtin_copysignf(__builtin_nansf("0x123"),-0.f);
static int calls;
static double arg(double x) { calls++; return x; }
int main(void) {
 for(int i=0;i<3;i++)for(int j=0;j<6;j++)if(normal[i][j]!=(j<4))return 1;
 for(int i=0;i<2;i++)for(int j=0;j<6;j++)if(nans[i][j]!=(j==5))return 2;
 if(infs[0]!=1||infs[1]!=-1||infs[2]!=-1||infs[3]||infs[4]||!precise||promoted)return 3;
 if(!__builtin_signbit(signzero)||magnitude!=0x1.000000000000001p0L)return 4;
 union {double f;unsigned long long u;} d={payload};
 union {float f;unsigned u;} f={fpayload};
 if(d.u!=0xfff0000000000123ULL||f.u!=0xff800123U)return 5;
 if(!__builtin_isgreater(arg(2),arg(1))||calls!=2)return 6;
 if(__builtin_isinf_sign((calls++, -__builtin_inf()))!=-1||calls!=3)return 7;
 if(__builtin_copysign(arg(2.),arg(-0.))!=-2.||calls!=5)return 8;
 return 0;
}
