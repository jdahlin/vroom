/* __float128 (x86-64): arithmetic, comparisons (NaN), conversions to and from
   every integer and float type (__int128 and x87 long double included),
   records, constants, compound assignment: libgcc soft float calls, the
   value in one xmm register across calls.  Elsewhere clang rejects it. */
#ifdef __SIZEOF_FLOAT128__
int printf(const char*, ...);
typedef __float128 Q;
static void pq(const char *s, Q q) { printf("%s %.17g %08x\n", s, (double)q, (unsigned)((*(unsigned __int128 *)&q) >> 96)); }
Q add(Q a, Q b) { return a + b; }
struct sq { Q q; }; struct sq rs(struct sq s) { s.q *= 2; return s; }
Q many(double a, Q b, int i, Q c, float f, long double l, Q d) { return a + b * i - c / f + l + d; }
int main(void) {
  volatile Q a = 1.5Q, b = 1e30Q; Q c = 1.0Q / 3;
  pq("add", add(a, b)); pq("third", c); pq("neg", -c); pq("mul", a * c);
  printf("cmp %d %d %d %d %d %d %d\n", a < b, a > b, a == a, a != b, a <= 1.5Q, a >= 2, !c);
  volatile Q z0 = 0; Q n = z0 / z0; printf("nan %d %d %d %d\n", n == n, n != n, n < a, (int)!!n);
  printf("conv %d %u %lld %llu %d %g %Lg\n", (int)-c * 3 - 7, (unsigned)(a * 2e9Q), (long long)(b / 1e12Q), (unsigned long long)(b / 1e12Q), (signed char)(a * 100), (float)c, (long double)c);
  __int128 w = (__int128)(b / 1e2Q); printf("i128 %lld %lld\n", (long long)(w >> 64), (long long)w);
  Q fw = (Q)w + (Q)(unsigned char)200 + (Q)-5 + (Q)4000000000u + (Q)(long double)0.25L + (Q)2.5f; pq("from", fw);
  struct sq s = rs((struct sq){ c }); pq("rec", s.q);
  pq("many", many(1.0, a, 3, c, 2.0f, 0.5L, b));
  Q x = 1; x += a; x++; x /= 2; pq("compound", x);
  return 0; }
#else
int main(void) { return 0; }
#endif
