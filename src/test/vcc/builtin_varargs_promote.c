/* The variable arguments of a library builtin get the default argument
   promotions: float becomes double, char and short int.  (macOS's snprintf
   is a macro for __builtin___snprintf_chk, same path; the test's libc has
   no _chk functions.  ffmpeg's av_opt printed float options as 0.000000.) */
int printf(const char *, ...);
volatile float f = 1.0f / 3;
volatile signed char sc = -5;
volatile unsigned short us = 65535;
int main(void)
{
	char b[64];
	float *p = (float *)&f;

	__builtin_printf("%f %d %u\n", f, sc, us);
	__builtin_snprintf(b, sizeof b, "%f %d %u", *p, sc, us);
	printf("%s\n", b);
	__builtin_sprintf(b, "%g %u", f * 3, us);
	printf("%s\n", b);
	return 0;
}
