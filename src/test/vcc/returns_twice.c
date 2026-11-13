/* setjmp/returns_twice: volatile locals changed between setjmp and longjmp
   keep their new values (C11 7.13.2.1p3), also around a loop of jumps */
#include <setjmp.h>
#include <stdio.h>

int my_setjmp(jmp_buf b) __attribute__((returns_twice));
static jmp_buf jb;
static int depth;

__attribute__((noinline)) static void jump(int v) { longjmp(jb, v); }

int main(void)
{
	volatile int n = 0, sum = 0;
	volatile long l = 1;
	volatile double d = 0.5;

	if (setjmp(jb) < 5) {
		n++;
		sum += n * 10;
		l *= 3;
		d *= 2;
		depth++;
		jump(n);
	}
	printf("%d %d %ld %g %d\n", n, sum, l, d, depth);
	return 0;
}
