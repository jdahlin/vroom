/* vcc-flags: -ffast-math -O0 */
static volatile int calls;
extern double pow(double, double) __asm__("custom_pow");
double custom_pow(double x, double y) { calls++; return x + y; }
int main(void) { return pow(16., .25) != 16.25 || calls != 1; }
