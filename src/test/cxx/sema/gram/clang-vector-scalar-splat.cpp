// A scalar operand of a vector operator converted to the element type when clang finds no truncation (vector_scalar_fits): a constant the element holds exactly, a non-constant of a type no greater than the element's, an integer no wider than the element's significand; values are clang++'s.
typedef float f4 __attribute__((vector_size(16)));
typedef double d2 __attribute__((vector_size(16)));
typedef unsigned u4 __attribute__((vector_size(16)));
f4 a3(f4 v) { return v + 0.5; }
f4 a4(f4 v) { return v + -0.0; }
f4 e1(f4 v) { return v + 16777216; }
f4 e7(f4 v) { return v * 3; }
f4 e10(f4 v, short s) { return v - s; }
f4 e12(f4 v) { return v + 5000000000LL; }
d2 e6(d2 v, float d) { return v + d; }
d2 b14(d2 v, int x) { return v / x; }
u4 x2(u4 v) { return v + -1; }
typedef char c16 __attribute__((vector_size(16)));
f4 r1(f4 v) { return v + 1.1; } // expected-error {{as implicit conversion would cause truncation}}
f4 r2(f4 v) { return v + 1e-50; } // expected-error {{as implicit conversion would cause truncation}}
f4 r3(f4 v) { return v + 16777217; } // expected-error {{as implicit conversion would cause truncation}}
f4 r4(f4 v, int i) { return v + i; } // expected-error {{as implicit conversion would cause truncation}}
f4 r5(f4 v, double d) { return v + d; } // expected-error {{as implicit conversion would cause truncation}}
d2 r6(d2 v, long long x) { return v + x; } // expected-error {{as implicit conversion would cause truncation}}
c16 r7(c16 v) { return v + 300; } // expected-error {{as implicit conversion would cause truncation}}
