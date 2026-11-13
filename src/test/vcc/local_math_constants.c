/* vcc-flags: -O2 -ffast-math */
/* Exact builtin folds require completed-function local provenance. */
static volatile unsigned source = 1024;
static unsigned global = 1024;
static int effects;
__attribute__((noinline)) static void replace(unsigned *p) { *p = source; }
__attribute__((noinline)) static unsigned once(void) { ++effects; return source; }
/* Fallback definitions keep this freestanding regression independent of libm.
   These implement the positive powers of two used below. */
__attribute__((noinline)) double log2(double x)
{
  double n = 0;
  while (x > 1) { x *= 0.5; n += 1; }
  return n;
}
__attribute__((noinline)) double pow(double x, double y)
{
  double r = 1;
  while (y > 0) { r *= x; y -= 1; }
  return r;
}
__attribute__((noinline)) static double mutated(void)
{
  unsigned a = 8;
  a = source;
  return __builtin_log2(a);
}
__attribute__((noinline)) static double escaped(void)
{
  unsigned a = 8;
  replace(&a);
  return __builtin_log2(a);
}
__attribute__((noinline)) static double jumped(void)
{
  unsigned a = 8;
  goto change;
  return __builtin_log2(a);
change:
  a = source;
  return __builtin_log2(a);
}
__attribute__((noinline)) static double conditional(void)
{
  unsigned a = 8;
  if (source) a = source;
  return __builtin_log2(a);
}
int main(void)
{
  unsigned long a = 1024, b = 16384;
  unsigned long result = __builtin_pow(2, (__builtin_log2(a) + __builtin_log2(b)) / 2);
  if (result != 4096) return 1;
  double base = 2, exponent = 10;
  if (__builtin_pow(base, exponent) != 1024 || __builtin_log2(0x1p-1000) != -1000) return 2;
  if (mutated() != 10 || escaped() != 10 || jumped() != 10 || conditional() != 10) return 3;
  volatile unsigned v = 8;
  v = source;
  if (__builtin_log2(v) != 10) return 4;
  global = source;
  if (__builtin_log2(global) != 10) return 5;
  if (__builtin_log2(once()) != 10 || effects != 1) return 6;
  unsigned incremented = 512;
  incremented *= 2;
  if (__builtin_log2(incremented) != 10) return 7;
  for (unsigned i = 1; i < 4; ++i) {
    unsigned local = 1u << i;
    if (__builtin_log2(local) != i) return 8;
  }
  return 0;
}
