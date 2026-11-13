/* vcc-flags: -O2 -ffast-math */
/* No libm definitions: successful linking checks that local propagation reaches
   the exact builtin folds (GCC PR84309). */
int main(void)
{
  unsigned long a = 1024, b = 16384;
  unsigned long c = __builtin_pow(2, (__builtin_log2(a) + __builtin_log2(b)) / 2);
  if (c != 4096) return 1;
  double two = 2, low = -1022, high = 1023;
  if (__builtin_pow(two, low) != 0x1p-1022) return 2;
  if (__builtin_pow(two, high) != 0x1p1023) return 3;
  double low_power = 0x1p-1022, high_power = 0x1p1023;
  if (__builtin_log2(low_power) != -1022 || __builtin_log2(high_power) != 1023) return 4;
  return 0;
}
