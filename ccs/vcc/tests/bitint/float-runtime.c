/* The caller supplies raw integer/IEEE bits. No wide integer crosses the
 * caller ABI, so the Python oracle also checks ARM independently of Clang. */
typedef unsigned _BitInt(WIDTH) U;
typedef _BitInt(WIDTH) S;
#define N ((WIDTH+63)/64)
static unsigned calls;
static U once_u(U x) { ++calls; return x; }
static S once_s(S x) { ++calls; return x; }
static FP once_f(FP x) { ++calls; return x; }
unsigned wide_to_float(const unsigned long long *in, unsigned sign,
                       unsigned long long *out) {
 union { U u; S s; unsigned long long w[N]; } x;
 union { FP f; unsigned long long w[2]; } y = {0};
 for(unsigned i=0;i<N;i++)x.w[i]=in[i];
 calls=0;
 y.f=sign?(FP)once_s(x.s):(FP)once_u(x.u);
 /* Bytes outside the FP representation are deliberately ignored by caller. */
 for(unsigned i=0;i<sizeof(FP);i++)((unsigned char*)out)[i]=((unsigned char*)&y)[i];
 return calls;
}
unsigned float_to_wide(const unsigned long long *in, unsigned sign,
                       unsigned long long *out) {
 union { FP f; unsigned long long w[2]; } x;
 union { U u; S s; unsigned long long w[N]; } y;
 for(unsigned i=0;i<sizeof(FP);i++)((unsigned char*)&x)[i]=((const unsigned char*)in)[i];
 calls=0;
 if(sign)y.s=(S)once_f(x.f);else y.u=(U)once_f(x.f);
 for(unsigned i=0;i<N;i++)out[i]=y.w[i];
 return calls;
}

void constant_to_float(unsigned long long *out) {
 union { FP f; unsigned long long w[2]; } x = {0};
 x.f=(FP)((U)1 << (WIDTH-2));
 for(unsigned i=0;i<sizeof(FP);i++)((unsigned char*)out)[i]=((unsigned char*)&x)[i];
}
void constant_from_float(unsigned long long *out) {
 union { U u; unsigned long long w[N]; } x;
 x.u=(U)0x1.8p100L;
 for(unsigned i=0;i<N;i++)out[i]=x.w[i];
}
