#ifndef _RT_MATH_H
#define _RT_MATH_H
#define HUGE_VAL __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
double fabs(double); float fabsf(float); double sqrt(double); float sqrtf(float);
double floor(double); double ceil(double); double trunc(double); double round(double);
double fmod(double, double); double copysign(double, double); double ldexp(double, int);
double frexp(double, int *); double pow(double, double); double exp(double); double log(double);
double sin(double); double cos(double); double tan(double); double atan(double); double atan2(double, double);
long double fabsl(long double);
double asin(double); double acos(double); double sinh(double); double cosh(double); double tanh(double);
double log10(double);
#endif
