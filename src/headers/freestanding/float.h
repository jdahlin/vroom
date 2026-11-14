/*
 * vcc's <float.h> (C11 5.2.4.2.2, C23 5.3.5.3.3): the characteristics of the
 * floating types, from the compiler's predefined __FLT_*__, __DBL_*__ and
 * __LDBL_*__ macros.
 *
 * mingw-w64's own <float.h> holds only the CRT's additions (_controlfp,
 * _clearfp, _fpclass, ...) and includes the compiler's with #include_next
 * unless that one has been read; so this one is first on the search path,
 * defines the standard macros, then lets mingw-w64's add its part.
 * _FLOAT_H___ is the guard mingw-w64's header looks for.
 */
#ifndef __VCC_FLOAT_H
#define __VCC_FLOAT_H
#define _FLOAT_H___

#define FLT_RADIX __FLT_RADIX__

#define FLT_MANT_DIG __FLT_MANT_DIG__
#define DBL_MANT_DIG __DBL_MANT_DIG__
#define LDBL_MANT_DIG __LDBL_MANT_DIG__

#if !defined(__STRICT_ANSI__) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#define DECIMAL_DIG __DECIMAL_DIG__
#endif
#if !defined(__STRICT_ANSI__) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L)
#define FLT_DECIMAL_DIG __FLT_DECIMAL_DIG__
#define DBL_DECIMAL_DIG __DBL_DECIMAL_DIG__
#define LDBL_DECIMAL_DIG __LDBL_DECIMAL_DIG__
#define FLT_HAS_SUBNORM __FLT_HAS_DENORM__
#define DBL_HAS_SUBNORM __DBL_HAS_DENORM__
#define LDBL_HAS_SUBNORM __LDBL_HAS_DENORM__
#define FLT_TRUE_MIN __FLT_DENORM_MIN__
#define DBL_TRUE_MIN __DBL_DENORM_MIN__
#define LDBL_TRUE_MIN __LDBL_DENORM_MIN__
#endif

#define FLT_DIG __FLT_DIG__
#define DBL_DIG __DBL_DIG__
#define LDBL_DIG __LDBL_DIG__

#define FLT_MIN_EXP __FLT_MIN_EXP__
#define DBL_MIN_EXP __DBL_MIN_EXP__
#define LDBL_MIN_EXP __LDBL_MIN_EXP__
#define FLT_MIN_10_EXP __FLT_MIN_10_EXP__
#define DBL_MIN_10_EXP __DBL_MIN_10_EXP__
#define LDBL_MIN_10_EXP __LDBL_MIN_10_EXP__
#define FLT_MAX_EXP __FLT_MAX_EXP__
#define DBL_MAX_EXP __DBL_MAX_EXP__
#define LDBL_MAX_EXP __LDBL_MAX_EXP__
#define FLT_MAX_10_EXP __FLT_MAX_10_EXP__
#define DBL_MAX_10_EXP __DBL_MAX_10_EXP__
#define LDBL_MAX_10_EXP __LDBL_MAX_10_EXP__

#define FLT_MAX __FLT_MAX__
#define DBL_MAX __DBL_MAX__
#define LDBL_MAX __LDBL_MAX__
#define FLT_EPSILON __FLT_EPSILON__
#define DBL_EPSILON __DBL_EPSILON__
#define LDBL_EPSILON __LDBL_EPSILON__
#define FLT_MIN __FLT_MIN__
#define DBL_MIN __DBL_MIN__
#define LDBL_MIN __LDBL_MIN__

/* the current rounding direction (1: to nearest) */
#define FLT_ROUNDS (__builtin_flt_rounds())

/* clang has __FLT_EVAL_METHOD__ as a builtin macro, vcc not yet (TODO.md);
   without it: x86-64 and arm64 evaluate each operation in its own type */
#ifdef __FLT_EVAL_METHOD__
#define FLT_EVAL_METHOD __FLT_EVAL_METHOD__
#elif defined(__x86_64__) || defined(__aarch64__)
#define FLT_EVAL_METHOD 0
#else
#define FLT_EVAL_METHOD (-1)
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define __STDC_VERSION_FLOAT_H__ 202311L
#define FLT_NORM_MAX __FLT_NORM_MAX__
#define DBL_NORM_MAX __DBL_NORM_MAX__
#define LDBL_NORM_MAX __LDBL_NORM_MAX__
#define FLT_IS_IEC_60559 1
#define DBL_IS_IEC_60559 1
#define LDBL_IS_IEC_60559 1
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define FLT_SNAN (__builtin_nansf(""))
#define DBL_SNAN (__builtin_nans(""))
#define LDBL_SNAN (__builtin_nansl(""))
#endif

#ifdef __FLT16_MANT_DIG__
/* _Float16 (ISO/IEC TS 18661-3), as GCC and clang name its macros */
#define FLT16_MANT_DIG __FLT16_MANT_DIG__
#define FLT16_DECIMAL_DIG __FLT16_DECIMAL_DIG__
#define FLT16_DIG __FLT16_DIG__
#define FLT16_MIN_EXP __FLT16_MIN_EXP__
#define FLT16_MIN_10_EXP __FLT16_MIN_10_EXP__
#define FLT16_MAX_EXP __FLT16_MAX_EXP__
#define FLT16_MAX_10_EXP __FLT16_MAX_10_EXP__
#define FLT16_MAX __FLT16_MAX__
#define FLT16_EPSILON __FLT16_EPSILON__
#define FLT16_MIN __FLT16_MIN__
#define FLT16_TRUE_MIN __FLT16_DENORM_MIN__
#endif

#endif /* __VCC_FLOAT_H */

/* the C library's part (mingw-w64: _controlfp and friends), behind its own guard */
#if defined(__MINGW32__) && __STDC_HOSTED__ && defined(__has_include_next)
#if __has_include_next(<float.h>)
#include_next <float.h>
#endif
#endif
