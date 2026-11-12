/* vcc-flags: -ffast-math */
extern double pow(double, double);
static volatile int calls;
static volatile double input;
static double once(void) { calls++; return input; }
int main(void)
{
  static const double values[] = { 0x0.0p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+4, 0x1.4400000000000p+6, 0x1.0000000000000p-4, 0x1.0000000000000p+8, 0x1.0000000000000p+1, 0x1.8000000000000p+1, 0x1.4000000000000p+3, 0x1.0000000000000p-1000, 0x1.0000000000000p+1000 };
  static const double expected[] = { 0x0.0p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+1, 0x1.8000000000000p+1, 0x1.0000000000000p-1, 0x1.0000000000000p+2, 0x1.306fe0a31b715p+0, 0x1.50ea39fcbf166p+0, 0x1.c73d51c54470ep+0, 0x1.0000000000000p-250, 0x1.0000000000000p+250 };
  for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++) {
    input = values[i];
    calls = 0;
    double result = pow(once(), 0.25);
    if (result != expected[i] || calls != 1) return 1;
  }
  return 0;
}
