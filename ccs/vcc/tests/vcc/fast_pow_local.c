/* vcc-flags: -ffast-math -O0 */
static volatile int calls;
static double pow(double x, double y) { calls++; return x + y; }
int main(void) { return pow(16., .25) != 16.25 || calls != 1; }
