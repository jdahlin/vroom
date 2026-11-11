/* Preserve signaling/quiet NaN encodings and payloads in globals and values. */
extern void abort(void);
static volatile float sf = __builtin_nansf("0x123");
static volatile double sd = __builtin_nans("0x123");
static volatile long double sl = __builtin_nansl("0x123");
static int calls;
static float once(void) { calls++; return sf; }
static int classify_f(float x) { return __builtin_issignaling(x); }
static int classify_d(double x) { return __builtin_issignaling(x); }
static int classify_l(long double x) { return __builtin_issignaling(x); }
int main(void) {
 union { float f; unsigned u; } f;
 union { double d; unsigned long long u; } d;
 f.f=__builtin_nansf(""); if(f.u!=0x7fa00000u) abort();
 f.f=__builtin_nansf("0x123"); if(f.u!=0x7f800123u) abort();
 f.f=__builtin_nanf("0x234"); if(f.u!=0x7fc00234u) abort();
 d.d=__builtin_nans(""); if(d.u!=0x7ff4000000000000ull) abort();
 d.d=__builtin_nans("0x123"); if(d.u!=0x7ff0000000000123ull) abort();
 d.d=__builtin_nan("0x234"); if(d.u!=0x7ff8000000000234ull) abort();
 if(!classify_f(sf)||!classify_d(sd)||!classify_l(sl)) abort();
 if(!__builtin_issignaling(once())||calls!=1) abort();
 if(classify_f(__builtin_nanf(""))||classify_d(__builtin_nan(""))||classify_l(__builtin_nanl(""))) abort();
 if(classify_f(0.0f)||classify_d(42.0)||classify_l(-0.0L)) abort();
 if(classify_f(__builtin_inff())||classify_d(-__builtin_inf())||classify_l(__builtin_infl())) abort();
 if(!classify_f(-sf)||!classify_d(-sd)||!classify_l(-sl)) abort();
 if (__builtin_issignaling((long double)__builtin_nansf("")) ||
     __builtin_issignaling((double)__builtin_nansf("")) ||
     __builtin_issignaling((float)__builtin_nansl(""))) abort();
 return 0;
}
