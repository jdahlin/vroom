/* x87 nonzero exponents require the explicit integer bit. */
extern void abort(void);
#if __LDBL_MANT_DIG__ == 64
static volatile unsigned long long patterns[][2] = {
 {0x3fffffffffffffffULL,0x7fff}, {1,0x7fff},
 {0x4000000000000000ULL,0x7fff}, {0,0x7fff},
 {0,1}, {0,0x7ffe}, {0x123,0x321},
 {0x3fffffffffffffffULL,0xffff}, {0,0x8001}
};
#endif
int main(void) {
#if __LDBL_MANT_DIG__ == 64
 for (unsigned i=0;i<sizeof(patterns)/sizeof(patterns[0]);i++) {
  union { long double f; unsigned long long u[2]; } x;
  x.u[0]=patterns[i][0]; x.u[1]=patterns[i][1];
  if (!__builtin_isnan(x.f) || __builtin_issignaling(x.f) ||
      __builtin_isinf(x.f) || __builtin_isfinite(x.f) ||
      __builtin_isnormal(x.f) || __builtin_issubnormal(x.f) ||
      __builtin_iszero(x.f) || __builtin_fpclassify(0,1,2,3,4,x.f)!=0)
   abort();
 }
#endif
 return 0;
}
