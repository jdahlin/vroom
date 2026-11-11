/* vcc-flags: -ffast-math -O0 */
static volatile int calls;
static double sqrt(double x) { calls++; return x + 1.; }
static double pow(double x,double y) { calls++; return x + y; }
extern float sqrtf(float) __asm__("user_root");
float user_root(float x) { calls++; return x + 2.f; }
int main(void)
{
  return sqrt(4.) != 5. || pow(2., 6.) != 8. || sqrtf(4.f) != 6.f || calls != 3;
}
