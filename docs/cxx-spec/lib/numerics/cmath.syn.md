# 26.8.1 Header `<cmath>` synopsis [cmath.syn]

[Contents](../../README.md) · up: [26.8 Mathematical functions for floating-point types \[c.math\]](c.math.md) · prev: [26.8 Mathematical functions for floating-point types \[c.math\]](c.math.md) · next: [26.8.2 Absolute values \[c.math.abs\]](c.math.abs.md)

```cpp
namespace std {
  using float_t = see below;
  using double_t = see below;
}

#define HUGE_VAL see below
#define HUGE_VALF see below
#define HUGE_VALL see below
#define INFINITY see below
#define NAN see below
#define FP_INFINITE see below
#define FP_NAN see below
#define FP_NORMAL see below
#define FP_SUBNORMAL see below
#define FP_ZERO see below
#define FP_FAST_FMA see below
#define FP_FAST_FMAF see below
#define FP_FAST_FMAL see below
#define FP_ILOGB0 see below
#define FP_ILOGBNAN see below
#define MATH_ERRNO see below
#define MATH_ERREXCEPT see below

#define math_errhandling see below

namespace std {
  float acos(float x);  // see [library.c]
  double acos(double x);
  long double acos(long double x);  // see [library.c]
  float acosf(float x);
  long double acosl(long double x);

  float asin(float x);  // see [library.c]
  double asin(double x);
  long double asin(long double x);  // see [library.c]
  float asinf(float x);
  long double asinl(long double x);

  float atan(float x);                  // see [library.c]
  double atan(double x);
  long double atan(long double x);      // see [library.c]
  float atanf(float x);
  long double atanl(long double x);

  float atan2(float y, float x);        // see [library.c]
  double atan2(double y, double x);
  long double atan2(long double y, long double x);  // see [library.c]
  float atan2f(float y, float x);
  long double atan2l(long double y, long double x);

  float cos(float x);                   // see [library.c]
  double cos(double x);
  long double cos(long double x);       // see [library.c]
  float cosf(float x);
  long double cosl(long double x);

  float sin(float x);                   // see [library.c]
  double sin(double x);
  long double sin(long double x);       // see [library.c]
  float sinf(float x);
  long double sinl(long double x);

  float tan(float x);                   // see [library.c]
  double tan(double x);
  long double tan(long double x);       // see [library.c]
  float tanf(float x);
  long double tanl(long double x);

  float acosh(float x);                 // see [library.c]
  double acosh(double x);
  long double acosh(long double x);     // see [library.c]
  float acoshf(float x);
  long double acoshl(long double x);

  float asinh(float x);                 // see [library.c]
  double asinh(double x);
  long double asinh(long double x);     // see [library.c]
  float asinhf(float x);
  long double asinhl(long double x);

  float atanh(float x);                 // see [library.c]
  double atanh(double x);
  long double atanh(long double x);     // see [library.c]
  float atanhf(float x);
  long double atanhl(long double x);

  float cosh(float x);                  // see [library.c]
  double cosh(double x);
  long double cosh(long double x);      // see [library.c]
  float coshf(float x);
  long double coshl(long double x);

  float sinh(float x);                  // see [library.c]
  double sinh(double x);
  long double sinh(long double x);      // see [library.c]
  float sinhf(float x);
  long double sinhl(long double x);

  float tanh(float x);                  // see [library.c]
  double tanh(double x);
  long double tanh(long double x);      // see [library.c]
  float tanhf(float x);
  long double tanhl(long double x);

  float exp(float x);                   // see [library.c]
  double exp(double x);
  long double exp(long double x);       // see [library.c]
  float expf(float x);
  long double expl(long double x);

  float exp2(float x);                  // see [library.c]
  double exp2(double x);
  long double exp2(long double x);      // see [library.c]
  float exp2f(float x);
  long double exp2l(long double x);

  float expm1(float x);                 // see [library.c]
  double expm1(double x);
  long double expm1(long double x);     // see [library.c]
  float expm1f(float x);
  long double expm1l(long double x);

  float frexp(float value, int* exp);   // see [library.c]
  double frexp(double value, int* exp);
  long double frexp(long double value, int* exp);   // see [library.c]
  float frexpf(float value, int* exp);
  long double frexpl(long double value, int* exp);

  int ilogb(float x);                   // see [library.c]
  int ilogb(double x);
  int ilogb(long double x);             // see [library.c]
  int ilogbf(float x);
  int ilogbl(long double x);

  float ldexp(float x, int exp);        // see [library.c]
  double ldexp(double x, int exp);
  long double ldexp(long double x, int exp);    // see [library.c]
  float ldexpf(float x, int exp);
  long double ldexpl(long double x, int exp);

  float log(float x);                   // see [library.c]
  double log(double x);
  long double log(long double x);       // see [library.c]
  float logf(float x);
  long double logl(long double x);

  float log10(float x);                 // see [library.c]
  double log10(double x);
  long double log10(long double x);     // see [library.c]
  float log10f(float x);
  long double log10l(long double x);

  float log1p(float x);                 // see [library.c]
  double log1p(double x);
  long double log1p(long double x);     // see [library.c]
  float log1pf(float x);
  long double log1pl(long double x);

  float log2(float x);                  // see [library.c]
  double log2(double x);
  long double log2(long double x);      // see [library.c]
  float log2f(float x);
  long double log2l(long double x);

  float logb(float x);                  // see [library.c]
  double logb(double x);
  long double logb(long double x);      // see [library.c]
  float logbf(float x);
  long double logbl(long double x);

  float modf(float value, float* iptr); // see [library.c]
  double modf(double value, double* iptr);
  long double modf(long double value, long double* iptr);   // see [library.c]
  float modff(float value, float* iptr);
  long double modfl(long double value, long double* iptr);

  float scalbn(float x, int n);         // see [library.c]
  double scalbn(double x, int n);
  long double scalbn(long double x, int n);     // see [library.c]
  float scalbnf(float x, int n);
  long double scalbnl(long double x, int n);

  float scalbln(float x, long int n);   // see [library.c]
  double scalbln(double x, long int n);
  long double scalbln(long double x, long int n);   // see [library.c]
  float scalblnf(float x, long int n);
  long double scalblnl(long double x, long int n);

  float cbrt(float x);                  // see [library.c]
  double cbrt(double x);
  long double cbrt(long double x);      // see [library.c]
  float cbrtf(float x);
  long double cbrtl(long double x);

  // [c.math.abs], absolute values
  int abs(int j);
  long int abs(long int j);
  long long int abs(long long int j);
  float abs(float j);
  double abs(double j);
  long double abs(long double j);

  float fabs(float x);                  // see [library.c]
  double fabs(double x);
  long double fabs(long double x);      // see [library.c]
  float fabsf(float x);
  long double fabsl(long double x);

  float hypot(float x, float y);        // see [library.c]
  double hypot(double x, double y);
  long double hypot(long double x, long double y);  // see [library.c]
  float hypotf(float x, float y);
  long double hypotl(long double x, long double y);

  // [c.math.hypot3], three-dimensional hypotenuse
  float hypot(float x, float y, float z);
  double hypot(double x, double y, double z);
  long double hypot(long double x, long double y, long double z);

  float pow(float x, float y);          // see [library.c]
  double pow(double x, double y);
  long double pow(long double x, long double y);    // see [library.c]
  float powf(float x, float y);
  long double powl(long double x, long double y);

  float sqrt(float x);                  // see [library.c]
  double sqrt(double x);
  long double sqrt(long double x);      // see [library.c]
  float sqrtf(float x);
  long double sqrtl(long double x);

  float erf(float x);                   // see [library.c]
  double erf(double x);
  long double erf(long double x);       // see [library.c]
  float erff(float x);
  long double erfl(long double x);

  float erfc(float x);                  // see [library.c]
  double erfc(double x);
  long double erfc(long double x);      // see [library.c]
  float erfcf(float x);
  long double erfcl(long double x);

  float lgamma(float x);                // see [library.c]
  double lgamma(double x);
  long double lgamma(long double x);    // see [library.c]
  float lgammaf(float x);
  long double lgammal(long double x);

  float tgamma(float x);                // see [library.c]
  double tgamma(double x);
  long double tgamma(long double x);    // see [library.c]
  float tgammaf(float x);
  long double tgammal(long double x);

  float ceil(float x);                  // see [library.c]
  double ceil(double x);
  long double ceil(long double x);      // see [library.c]
  float ceilf(float x);
  long double ceill(long double x);

  float floor(float x);                 // see [library.c]
  double floor(double x);
  long double floor(long double x);     // see [library.c]
  float floorf(float x);
  long double floorl(long double x);

  float nearbyint(float x);             // see [library.c]
  double nearbyint(double x);
  long double nearbyint(long double x); // see [library.c]
  float nearbyintf(float x);
  long double nearbyintl(long double x);

  float rint(float x);                  // see [library.c]
  double rint(double x);
  long double rint(long double x);      // see [library.c]
  float rintf(float x);
  long double rintl(long double x);

  long int lrint(float x);              // see [library.c]
  long int lrint(double x);
  long int lrint(long double x);        // see [library.c]
  long int lrintf(float x);
  long int lrintl(long double x);

  long long int llrint(float x);        // see [library.c]
  long long int llrint(double x);
  long long int llrint(long double x);  // see [library.c]
  long long int llrintf(float x);
  long long int llrintl(long double x);

  float round(float x);                 // see [library.c]
  double round(double x);
  long double round(long double x);     // see [library.c]
  float roundf(float x);
  long double roundl(long double x);

  long int lround(float x);             // see [library.c]
  long int lround(double x);
  long int lround(long double x);       // see [library.c]
  long int lroundf(float x);
  long int lroundl(long double x);

  long long int llround(float x);       // see [library.c]
  long long int llround(double x);
  long long int llround(long double x); // see [library.c]
  long long int llroundf(float x);
  long long int llroundl(long double x);

  float trunc(float x);                 // see [library.c]
  double trunc(double x);
  long double trunc(long double x);     // see [library.c]
  float truncf(float x);
  long double truncl(long double x);

  float fmod(float x, float y);         // see [library.c]
  double fmod(double x, double y);
  long double fmod(long double x, long double y);   // see [library.c]
  float fmodf(float x, float y);
  long double fmodl(long double x, long double y);

  float remainder(float x, float y);    // see [library.c]
  double remainder(double x, double y);
  long double remainder(long double x, long double y);  // see [library.c]
  float remainderf(float x, float y);
  long double remainderl(long double x, long double y);

  float remquo(float x, float y, int* quo);     // see [library.c]
  double remquo(double x, double y, int* quo);
  long double remquo(long double x, long double y, int* quo);   // see [library.c]
  float remquof(float x, float y, int* quo);
  long double remquol(long double x, long double y, int* quo);

  float copysign(float x, float y);     // see [library.c]
  double copysign(double x, double y);
  long double copysign(long double x, long double y);   // see [library.c]
  float copysignf(float x, float y);
  long double copysignl(long double x, long double y);

  double nan(const char* tagp);
  float nanf(const char* tagp);
  long double nanl(const char* tagp);

  float nextafter(float x, float y);    // see [library.c]
  double nextafter(double x, double y);
  long double nextafter(long double x, long double y);  // see [library.c]
  float nextafterf(float x, float y);
  long double nextafterl(long double x, long double y);

  float nexttoward(float x, long double y);     // see [library.c]
  double nexttoward(double x, long double y);
  long double nexttoward(long double x, long double y);     // see [library.c]
  float nexttowardf(float x, long double y);
  long double nexttowardl(long double x, long double y);

  float fdim(float x, float y);         // see [library.c]
  double fdim(double x, double y);
  long double fdim(long double x, long double y);   // see [library.c]
  float fdimf(float x, float y);
  long double fdiml(long double x, long double y);

  float fmax(float x, float y);         // see [library.c]
  double fmax(double x, double y);
  long double fmax(long double x, long double y);   // see [library.c]
  float fmaxf(float x, float y);
  long double fmaxl(long double x, long double y);

  float fmin(float x, float y);         // see [library.c]
  double fmin(double x, double y);
  long double fmin(long double x, long double y);   // see [library.c]
  float fminf(float x, float y);
  long double fminl(long double x, long double y);

  float fma(float x, float y, float z); // see [library.c]
  double fma(double x, double y, double z);
  long double fma(long double x, long double y, long double z); // see [library.c]
  float fmaf(float x, float y, float z);
  long double fmal(long double x, long double y, long double z);

  // [c.math.lerp], linear interpolation
  constexpr float lerp(float a, float b, float t) noexcept;
  constexpr double lerp(double a, double b, double t) noexcept;
  constexpr long double lerp(long double a, long double b, long double t) noexcept;

  // [c.math.fpclass], classification / comparison functions
  int fpclassify(float x);
  int fpclassify(double x);
  int fpclassify(long double x);

  bool isfinite(float x);
  bool isfinite(double x);
  bool isfinite(long double x);

  bool isinf(float x);
  bool isinf(double x);
  bool isinf(long double x);

  bool isnan(float x);
  bool isnan(double x);
  bool isnan(long double x);

  bool isnormal(float x);
  bool isnormal(double x);
  bool isnormal(long double x);

  bool signbit(float x);
  bool signbit(double x);
  bool signbit(long double x);

  bool isgreater(float x, float y);
  bool isgreater(double x, double y);
  bool isgreater(long double x, long double y);

  bool isgreaterequal(float x, float y);
  bool isgreaterequal(double x, double y);
  bool isgreaterequal(long double x, long double y);

  bool isless(float x, float y);
  bool isless(double x, double y);
  bool isless(long double x, long double y);

  bool islessequal(float x, float y);
  bool islessequal(double x, double y);
  bool islessequal(long double x, long double y);

  bool islessgreater(float x, float y);
  bool islessgreater(double x, double y);
  bool islessgreater(long double x, long double y);

  bool isunordered(float x, float y);
  bool isunordered(double x, double y);
  bool isunordered(long double x, long double y);

  // [sf.cmath], mathematical special functions

  // [sf.cmath.assoc.laguerre], associated Laguerre polynomials
  double       assoc_laguerre(unsigned n, unsigned m, double x);
  float        assoc_laguerref(unsigned n, unsigned m, float x);
  long double  assoc_laguerrel(unsigned n, unsigned m, long double x);

  // [sf.cmath.assoc.legendre], associated Legendre functions
  double       assoc_legendre(unsigned l, unsigned m, double x);
  float        assoc_legendref(unsigned l, unsigned m, float x);
  long double  assoc_legendrel(unsigned l, unsigned m, long double x);

  // [sf.cmath.beta], beta function
  double       beta(double x, double y);
  float        betaf(float x, float y);
  long double  betal(long double x, long double y);

  // [sf.cmath.comp.ellint.1], complete elliptic integral of the first kind
  double       comp_ellint_1(double k);
  float        comp_ellint_1f(float k);
  long double  comp_ellint_1l(long double k);

  // [sf.cmath.comp.ellint.2], complete elliptic integral of the second kind
  double       comp_ellint_2(double k);
  float        comp_ellint_2f(float k);
  long double  comp_ellint_2l(long double k);

  // [sf.cmath.comp.ellint.3], complete elliptic integral of the third kind
  double       comp_ellint_3(double k, double nu);
  float        comp_ellint_3f(float k, float nu);
  long double  comp_ellint_3l(long double k, long double nu);

  // [sf.cmath.cyl.bessel.i], regular modified cylindrical Bessel functions
  double       cyl_bessel_i(double nu, double x);
  float        cyl_bessel_if(float nu, float x);
  long double  cyl_bessel_il(long double nu, long double x);

  // [sf.cmath.cyl.bessel.j], cylindrical Bessel functions of the first kind
  double       cyl_bessel_j(double nu, double x);
  float        cyl_bessel_jf(float nu, float x);
  long double  cyl_bessel_jl(long double nu, long double x);

  // [sf.cmath.cyl.bessel.k], irregular modified cylindrical Bessel functions
  double       cyl_bessel_k(double nu, double x);
  float        cyl_bessel_kf(float nu, float x);
  long double  cyl_bessel_kl(long double nu, long double x);

  // [sf.cmath.cyl.neumann], cylindrical Neumann functions;
  // cylindrical Bessel functions of the second kind
  double       cyl_neumann(double nu, double x);
  float        cyl_neumannf(float nu, float x);
  long double  cyl_neumannl(long double nu, long double x);

  // [sf.cmath.ellint.1], incomplete elliptic integral of the first kind
  double       ellint_1(double k, double phi);
  float        ellint_1f(float k, float phi);
  long double  ellint_1l(long double k, long double phi);

  // [sf.cmath.ellint.2], incomplete elliptic integral of the second kind
  double       ellint_2(double k, double phi);
  float        ellint_2f(float k, float phi);
  long double  ellint_2l(long double k, long double phi);

  // [sf.cmath.ellint.3], incomplete elliptic integral of the third kind
  double       ellint_3(double k, double nu, double phi);
  float        ellint_3f(float k, float nu, float phi);
  long double  ellint_3l(long double k, long double nu, long double phi);

  // [sf.cmath.expint], exponential integral
  double       expint(double x);
  float        expintf(float x);
  long double  expintl(long double x);

  // [sf.cmath.hermite], Hermite polynomials
  double       hermite(unsigned n, double x);
  float        hermitef(unsigned n, float x);
  long double  hermitel(unsigned n, long double x);

  // [sf.cmath.laguerre], Laguerre polynomials
  double       laguerre(unsigned n, double x);
  float        laguerref(unsigned n, float x);
  long double  laguerrel(unsigned n, long double x);

  // [sf.cmath.legendre], Legendre polynomials
  double       legendre(unsigned l, double x);
  float        legendref(unsigned l, float x);
  long double  legendrel(unsigned l, long double x);

  // [sf.cmath.riemann.zeta], Riemann zeta function
  double       riemann_zeta(double x);
  float        riemann_zetaf(float x);
  long double  riemann_zetal(long double x);

  // [sf.cmath.sph.bessel], spherical Bessel functions of the first kind
  double       sph_bessel(unsigned n, double x);
  float        sph_besself(unsigned n, float x);
  long double  sph_bessell(unsigned n, long double x);

  // [sf.cmath.sph.legendre], spherical associated Legendre functions
  double       sph_legendre(unsigned l, unsigned m, double theta);
  float        sph_legendref(unsigned l, unsigned m, float theta);
  long double  sph_legendrel(unsigned l, unsigned m, long double theta);

  // [sf.cmath.sph.neumann], spherical Neumann functions;
  // spherical Bessel functions of the second kind
  double       sph_neumann(unsigned n, double x);
  float        sph_neumannf(unsigned n, float x);
  long double  sph_neumannl(unsigned n, long double x);
}
```

**1** The contents and meaning of the header `<cmath>` are the same as the C standard library header `<math.h>`, with the addition of a three-dimensional hypotenuse function ([[c.math.hypot3]](c.math.hypot3.md)) and the mathematical special functions described in [[sf.cmath]](sf.cmath.md). [*Note 1*: Several functions have additional overloads in this document, but they have the same behavior as in the C standard library ([[library.c]](../library/library.c.md)). — *end note*]

**2** For each set of overloaded functions within `<cmath>`, with the exception of `abs`, there shall be additional overloads sufficient to ensure:

- If any argument of arithmetic type corresponding to a `double` parameter has type `long double`, then all arguments of arithmetic type ([[basic.fundamental]](../../basic/basic.fundamental.md)) corresponding to `double` parameters are effectively cast to `long double`.
- Otherwise, if any argument of arithmetic type corresponding to a `double` parameter has type `double` or an integer type, then all arguments of arithmetic type corresponding to `double` parameters are effectively cast to `double`.
- [*Note 2*: Otherwise, all arguments of arithmetic type corresponding to `double` parameters have type `float`. — *end note*]

[*Note 3*: `abs` is exempted from these rules in order to stay compatible with C. — *end note*]

*See also:* ISO C 7.12
