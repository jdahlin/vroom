/* setjmp/longjmp: several setjmp sites in one function, a longjmp from a
   callee and from a loop, values kept in volatile locals across the jump.
   wasm lowers it to __wasm_setjmp and a try/catch of __c_longjmp */
#include <setjmp.h>
#include <stdio.h>
static jmp_buf b, b2;
static int n;
__attribute__((noinline)) static void jump(jmp_buf e, int v) { longjmp(e, v); }
int main(void)
{
	volatile int k = 0, s = 0;
	int r = setjmp(b);

	printf("r=%d k=%d\n", r, k);
	if (k++ < 3)
		jump(b, k * 10);
	if (!setjmp(b)) {
		n++;
		jump(b, 0);	/* longjmp(.., 0) returns 1 */
	}
	for (volatile int i = 0; i < 4; i++)
		if (setjmp(b2) == 0)
			jump(b2, i + 1);
		else
			s += i;
	printf("n=%d s=%d\n", n, s);
	return 0;
}
