/* vcc-flags: -ffast-math -fno-fast-math */
extern double pow(double, double);
static volatile double input = -0.0;
int main(void)
{
  union { double d; unsigned long long bits; } result;
  result.d = pow(input, 0.25);
  /* pow(-0, positive nonintegral) is +0; two square roots retain -0. */
  return result.bits != 0;
}
