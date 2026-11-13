/* vcc-flags: -ffast-math */
extern double pow(double,double), sqrt(double);
extern float sqrtf(float);
static volatile int calls;
static double once(double x) { calls++; return x; }
static double sixth(double x) { return pow(x, 6); }
static double inverse_fourth(double x) { return pow(x, -4); }
static double sixtyfourth(double x) { return __builtin_pow(x, 64.); }
static double inverse_sixtyfourth(double x) { return __builtin_pow(x, -64.); }
int main(void)
{
  double folded = __builtin_sqrt(4.0);
  volatile double x=2., y=81.;
  volatile float f=0.25f;
  if (sixth(x)!=64. || inverse_fourth(x)!=0.0625) return 1;
  if (sixtyfourth(x)!=0x1p64 || inverse_sixtyfourth(x)!=0x1p-64) return 2;
  if (sqrt(y)!=9. || sqrtf(f)!=0.5f || __builtin_sqrt(y)!=9. || __builtin_sqrtf(f)!=0.5f) return 3;
  if (pow(once(x), 0.)!=1. || calls!=1) return 4;
  if (pow(once(x), 5.)!=32. || calls!=2) return 5;
  if (pow(once(y), .5)!=9. || calls!=3 || folded!=2.) return 6;
  return 0;
}
