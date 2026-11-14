/* Windows x64 (PLAN-win64.md WIN3, validation point 4): UCRT's longjmp with a frame (what MinGW's
   setjmp passes: __builtin_frame_address(0)) unwinds with RtlUnwindEx through every frame between,
   restoring the callee-saved registers from their prologues' records, and stops at the frame whose
   establisher frame is the one setjmp saw. */
#include <stdio.h>
#include "winapi.h"

#define NI __attribute__((noinline))

typedef struct __attribute__((aligned(16))) { u64 p[2]; } jbt;
typedef jbt jbuf[16];
int ucrt_setjmp(jbuf, void *) __asm__("_setjmp") __attribute__((returns_twice));
void ucrt_longjmp(jbuf, int) __asm__("longjmp") __attribute__((noreturn));

static jbuf jb;
static volatile int sink;

NI void deep3(int x)
{
	/* every callee-saved register written: the unwind must restore them for the frames above */
	__asm__ volatile("xorl %%ebx, %%ebx; xorl %%esi, %%esi; xorl %%edi, %%edi; xorl %%r12d, %%r12d; xorl %%r13d, %%r13d;"
			 "xorl %%r14d, %%r14d; xorl %%r15d, %%r15d; xorps %%xmm6, %%xmm6; xorps %%xmm7, %%xmm7; xorps %%xmm8, %%xmm8;"
			 "xorps %%xmm9, %%xmm9; xorps %%xmm10, %%xmm10; xorps %%xmm11, %%xmm11; xorps %%xmm12, %%xmm12;"
			 "xorps %%xmm13, %%xmm13; xorps %%xmm14, %%xmm14; xorps %%xmm15, %%xmm15" ::: "rbx", "rsi", "rdi", "r12", "r13",
			 "r14", "r15", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
	if (x)
		ucrt_longjmp(jb, x);
	sink = x;
}

NI int deep2(int x)
{
	volatile char *p = __builtin_alloca((unsigned)x * 100 + 16);

	p[0] = 1;
	deep3(x);
	return p[0];
}

NI int deep1(int x)
{
	volatile char big[10000];

	big[9999] = (char)x;
	big[5] = (char)x;
	return deep2(x) + big[5] + big[9999];
}

/* the setjmp in a function of its own, its caller's values in callee-saved registers */
NI int catcher(int x)
{
	volatile int calls = 0;
	int r;

	/* a save area below the frame pointer: the establisher frame is not rbp */
	__asm__ volatile("" ::: "rbx", "rsi", "rdi", "xmm6", "xmm7");
	r = ucrt_setjmp(jb, __builtin_frame_address(0));
	if (r == 0) {
		calls++;
		deep1(x);
		return -1;
	}
	return r * 10 + calls;
}

NI double user(int x)
{
	double a = sink + 0.5, b = a * 3;
	long long u = sink + 1000, v = u * 7;
	int k = catcher(x);

	return k + a + b + (double)(u + v);
}

int main(void)
{
	volatile int calls = 0;
	int r = ucrt_setjmp(jb, __builtin_frame_address(0));

	if (r == 0) {
		calls++;
		deep1(7);
		printf("not reached\n");
	}
	printf("main: %d %d\n", r, calls);
	sink = 2;
	printf("user: %g\n", user(5));
	return 0;
}
