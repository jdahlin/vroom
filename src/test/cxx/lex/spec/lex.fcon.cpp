// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++11 -fsyntax-only -verify %s
//
// [lex.fcon] (docs/cxx-spec/lex/lex.fcon.md): types by suffix (Table 10) and
// values; bench/cxx-lex.py checks each value's bits against clang.

// [lex.fcon]/2 Example 1: "The floating-point-literals 49.625 and 0xC.68p+2
// have the same value.  The floating-point-literals 1.602'176'565e-19 and
// 1.602176565e-19 have the same value."
double example1a = 49.625, example1b = 0xC.68p+2; // expected-warning 0-1 {{hexadecimal floating literals}}
double example1c = 1.602176565e-19;
#if __cplusplus >= 201402L
double example1d = 1.602'176'565e-19;
#endif

// Table 10: none double, f or F float, l or L long double
float f1 = 1.5f, f2 = 1.5F;
long double l1 = 1.5l, l2 = 1.5L;

// [lex.fcon]/3: "the larger or smaller representable value nearest the
// scaled value": nearest, ties to even, also among the subnormals
double nearest = 0.1, tie = 9007199254740993.0; // 2^53 + 1 ties to 2^53
double smallest_normal = 2.2250738585072014e-308;
double largest_subnormal = 2.2250738585072009e-308;
double subnormal = 1e-310, subnormal2 = 3.5e-320, subnormal3 = 2.4703282292062328e-324;
double smallest_subnormal = 4.9406564584124654e-324;
double half_smallest = 2.4703282292062327e-324; // expected-warning {{too small}}
float float_subnormal = 1e-40f, float_subnormal2 = 7.006492e-46f;
float float_smallest = 1.401298464e-45f, float_half = 7.00649e-46f; // expected-warning {{too small}}
long double long_subnormal = 1e-4940L, long_tiny = 1e-4965L;

// [lex.fcon]/3: "If the scaled value is not in the range of representable
// values for its type, the program is ill-formed."  clang warns.
double too_large = 1e309;      // expected-warning {{too large}}
float too_large_f = 1e39f;     // expected-warning {{too large}}
double too_small = 1e-400;     // expected-warning {{too small}}
