/* vcc-xfail-wasm: TODO.md _BitInt remaining targets: wasm runtime ABI not implemented. */
/* vcc-xfail-host: TODO.md _BitInt remaining targets: Darwin runtime ABI not implemented. */
#include "bitint.h"
#define CALLBACK(N) S##N callback##N(S##N a) { return a; }
CALLBACK(2) CALLBACK(7) CALLBACK(9) CALLBACK(17) CALLBACK(31) CALLBACK(32) CALLBACK(33)
CALLBACK(63) CALLBACK(64) CALLBACK(65) CALLBACK(96) CALLBACK(127) CALLBACK(128)
int main(void) {
 U pattern = ((U)0xfedcba9876543210UL << 64) | 0xefcdab8967452301UL;
#define CHECK(N) { \
 U##N u = (U##N)pattern; \
 S##N s = (S##N)pattern; \
 if (unsigned##N(u) != u || signed##N(s) != s || call##N(s) != s) return N; \
 if (stack##N(11,12,13,14,15,u,16,17,18) != u) return N+64; \
 }
 CHECK(2) CHECK(7) CHECK(9) CHECK(17) CHECK(31) CHECK(32) CHECK(33)
 CHECK(63) CHECK(64) CHECK(65) CHECK(96) CHECK(127) CHECK(128)
 if (one(1) != 1 || one(0) != 0) return 1;
 if (stackarm(11,12,13,14,15,16,17,(U65)pattern,18) != (U65)pattern) return 3;
 U128 expected = (U128)pattern ^ (U128)(U7)pattern ^ (U128)(U65)pattern ^ (U128)(U7)(pattern>>1) ^ (U128)(pattern+1) ^ (U128)(pattern+2);
 if (variadic(0,(U128)pattern,(U7)pattern,(U65)pattern,(U7)(pattern>>1),(U128)(pattern+1),(U128)(pattern+2)) != expected) return 4;
 struct R a = { 5, (U65)pattern, -17 }, b = record(a);
 if (b.a != 5 || b.b != (a.b ^ ((U65)1 << 64)) || b.c != 17) return 5;
 return 0;
}
