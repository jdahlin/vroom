/* Library spellings do not identify static or asm-renamed user functions. */
static double fabs(double x) { return -x; }
extern double floor(double) __asm__("custom_floor");
__attribute__((noinline)) double custom_floor(double x) { return x + 100.0; }
int main(void)
{
	volatile float x = 1.25f;
	if (fabs(x) != -1.25) return 1;
	if (floor(1.25) != 101.25 || floor(x) != 101.25) return 2;
	return 0;
}
