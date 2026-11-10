typedef int v4 __attribute__((vector_size(16)));
typedef short s8 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));
typedef double d2 __attribute__((vector_size(16)));
typedef char c16 __attribute__((vector_size(16)));
void t(v4 a, s8 b, f4 f, d2 d, c16 ch, int i, long l, short sh, float fl, double db, char cc) {
  a = a + 1.5; a = a + 1.0; a = a + i; a = a + l; a = a + 5000000000; a = a + sh;
  b = b + 1; b = b + 70000; b = b + i; b = b + sh; b = b + cc;
  f = f + 1.5; f = f + 0.1; f = f + db; f = f + fl; f = f + i; f = f + l; f = f + 16777217; f = f + 3;
  d = d + l; d = d + i; d = d + fl;
  ch = ch + 300; ch = ch + 'a'; ch = ch + i;
  a = a + 2u; a = a + 3ul; a = a + (unsigned)i;
}
void t1(v4 a, long l) { a = a + 1.5; a = l * a; a += 1.5; a = a < l; }
