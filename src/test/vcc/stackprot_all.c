/* -fstack-protector-all: every function keeps a canary below its saved frame
   and checks it before returning; recursion, arrays, varargs still work. */
/* vcc-flags: -fstack-protector-all */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

/* (the bare test runtime has no guard: one here, weak, as a libc's) */
__attribute__((weak)) unsigned long __stack_chk_guard = 0x5a17c0dedeadbeefUL;
__attribute__((weak, noreturn)) void __stack_chk_fail(void) { abort(); }

static int sum(int n, ...)
{
	va_list ap;
	int s = 0;

	va_start(ap, n);
	for (int i = 0; i < n; i++)
		s += va_arg(ap, int);
	va_end(ap);
	return s;
}

static long fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int main(void)
{
	char buf[32];

	strcpy(buf, "canary");
	printf("%s %d %ld %zu\n", buf, sum(4, 1, 2, 3, 4), fib(20), strlen(buf));
	return 0;
}
