/* GNU vectors of __int128 and long double: arithmetic, shifts, comparisons, unary ops
   lane by lane (the lanes are objects held by address). */
/* vcc-xfail: arm64: unary minus on __int128 vector lanes keeps 32 bits (TODO.md tier 1) */
int printf(const char*, ...);
typedef __int128 W __attribute__((vector_size(32))); typedef unsigned __int128 U __attribute__((vector_size(32)));
typedef long double L __attribute__((vector_size(32)));
int main(void){ volatile long k = 3; W a = { (W){0}[0] + k, -k }, b = { 5, -1 }; U u = { ~(unsigned __int128)0, 7 };
 W s = a + b * (W){2, 2}, m = -a, c = a < b, x = ~b ^ a; U d = u >> (U){1, 1}, f = u / (U){3, 3}; L l = { 1.5L, -2 }, q = l * l - (L){1, 1};
 printf("%ld %ld %ld %ld %ld %ld %ld %ld\n", (long)s[0], (long)s[1], (long)m[0], (long)c[0], (long)c[1], (long)x[1], (long)(d[0] >> 64), (long)f[1]);
 printf("%Lg %Lg %d\n", q[0], q[1], (int)(q < l)[0]); return 0; }
