/* An asm-renamed function named fabs need not return a nonnegative value. */
extern double fabs(double) __asm__("custom_fabs");
extern float fabsf(float) __asm__("custom_fabsf");
__attribute__((noinline)) double custom_fabs(double x) { return -x; }
__attribute__((noinline)) float custom_fabsf(float x) { return -x; }
__attribute__((noinline)) int check_double(double x)
{
	double a = fabs(x), zero = 0.0;
	return a < zero;
}
__attribute__((noinline)) int check_float(float x)
{
	float a = fabsf(x), zero = 0.0f;
	return a < zero;
}
int main(void)
{
	volatile double x = 1.25;
	return !check_double(x) || !check_float((float)x);
}
