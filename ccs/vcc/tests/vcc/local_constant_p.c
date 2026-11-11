/* vcc-flags: -O2 */
static volatile unsigned source = 7;
static int effects;
__attribute__((noinline)) static int unknown(unsigned x)
{
  return __builtin_constant_p(x);
}
int main(void)
{
  unsigned a = 3, b = a * 7;
  double d = 4;
  if (!__builtin_constant_p(b + 1)) return 1;
  if (!__builtin_constant_p(d)) return 2;
  if (__builtin_constant_p(source)) return 3;
  if (__builtin_constant_p(++effects) || effects) return 4;
  if (unknown(source)) return 5;
  return 0;
}
