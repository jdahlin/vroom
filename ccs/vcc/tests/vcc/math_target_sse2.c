/* vcc-flags: -mgeneral-regs-only -ffast-math */
#if defined(__x86_64__)
extern double fabs(double), copysign(double,double), pow(double,double);
__attribute__((target("sse2"))) static double operation(double x)
{
  return fabs(x) + copysign(x, -0.0) + pow(x, 0.25);
}
__attribute__((target("sse2"))) int main(void)
{
  volatile double value = 16.;
  return operation(value) != 2.;
}
#else
int main(void) { return 0; }
#endif
