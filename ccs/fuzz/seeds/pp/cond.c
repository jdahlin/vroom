#if defined(A) || (1 << 3) == 8 && -1 < 0u
yes1
#elif 1/0
#else
no
#endif
#if 0x7fffffffffffffff + 0 > 0 && 'a' == 97 && '\377' < 0 && ~0u == 0xffffffff
yes2
#endif
#ifdef __STDC__
#ifndef NOPE
#if __has_include("a.h") && !__has_include(<nope.h>) && __has_attribute(packed) && __has_builtin(__builtin_expect)
yes3 __LINE__ __FILE__ __COUNTER__ __COUNTER__ __INCLUDE_LEVEL__
#endif
#endif
#endif
#if (2 || 1/0) ? 3 : 1/0
yes4
#endif
#define E(x) defined(x)
#if E(E) && __STDC_VERSION__ >= 201112L
yes5
#endif
#elifdef X
#line 100 "foo.c"
__LINE__ __FILE__
_Pragma("GCC diagnostic push") _Pragma("message(\"hi\")")
#pragma GCC poison bad
#ident "x"
#warning hello
#error stop
done
