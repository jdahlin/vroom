/* Windows x64's rules (PLAN-win64.md WIN2/WIN3), each against clang's build: the shadow space a
   variadic CRT callee spills into, records by reference to a copy the callee may change, varargs
   with doubles (in both registers), the callee-saved rsi/rdi/rbx/r12-r15/xmm6-15 kept across a
   CRT callback that clobbers them all, frames and alloca of many pages, __main's constructors,
   native TLS (two threads), dllimport calls, .refptr data, weak symbols. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winapi.h"

#define NI __attribute__((noinline))

static int order[8], norder;
__attribute__((constructor(200))) static void ctor_late(void) { order[norder++] = 2; }
__attribute__((constructor(101))) static void ctor_early(void) { order[norder++] = 1; }
__attribute__((constructor)) static void ctor_default(void) { order[norder++] = 3; }
__attribute__((destructor)) static void dtor(void) { norder = -1; }

struct r3 { char c[3]; };
struct r8 { int a, b; };
struct r12 { int a, b, c; };
struct r40 { double d[5]; };

/* the callee changes its by-reference copies: the caller's objects stay */
NI int byref(struct r3 a, struct r8 b, struct r12 c, struct r40 d, long double e, __int128 f)
{
	int s = a.c[0] + a.c[2] + b.a + b.b + c.a + c.c + (int)d.d[0] + (int)d.d[4] + (int)e + (int)f;

	a.c[0] = 99, b.a = 99, c.a = 99, d.d[0] = 99, e = 99, f = 99;
	return s + a.c[0] + b.a + c.a + (int)d.d[0] + (int)e + (int)f;
}

NI double vsum(int n, ...)
{
	__builtin_va_list ap;
	double s = 0;

	__builtin_va_start(ap, n);
	for (int i = 0; i < n; i++)
		switch (i % 5) {
		case 0: s += __builtin_va_arg(ap, double); break;
		case 1: s += __builtin_va_arg(ap, int); break;
		case 2: s += __builtin_va_arg(ap, struct r12).c; break;
		case 3: s += (double)__builtin_va_arg(ap, long double); break;
		default: s += __builtin_va_arg(ap, struct r8).b; break;
		}
	__builtin_va_end(ap);
	return s;
}

/* a qsort comparator that clobbers every callee-saved register (saved by its prologue) */
static int cmp(const void *a, const void *b)
{
	__asm__ volatile("xorl %%ebx, %%ebx; xorl %%esi, %%esi; xorl %%edi, %%edi; xorl %%r12d, %%r12d; xorl %%r13d, %%r13d;"
			 "xorl %%r14d, %%r14d; xorl %%r15d, %%r15d; xorps %%xmm6, %%xmm6; xorps %%xmm7, %%xmm7; xorps %%xmm8, %%xmm8;"
			 "xorps %%xmm9, %%xmm9; xorps %%xmm10, %%xmm10; xorps %%xmm11, %%xmm11; xorps %%xmm12, %%xmm12;"
			 "xorps %%xmm13, %%xmm13; xorps %%xmm14, %%xmm14; xorps %%xmm15, %%xmm15" ::: "rbx", "rsi", "rdi", "r12", "r13",
			 "r14", "r15", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
	return *(const int *)a - *(const int *)b;
}

/* pages of locals and of alloca, the far end touched first: Windows commits the stack a guard
   page at a time, so only ___chkstk_ms's probes make this work */
NI int deep_frame(int k)
{
	volatile char big[100000];
	volatile char *p = __builtin_alloca((unsigned)k * 50000);

	p[0] = (char)k;
	big[0] = (char)k;
	return big[0] + p[0] + p[k * 50000 - 1];
}

_Thread_local int tls_init = 42;
_Thread_local int tls_zero;
static _Thread_local long long tls_static = 5;

static u32 thread(void *arg)
{
	tls_init += (int)(u64)arg;
	tls_zero++;
	tls_static *= 3;
	return (u32)(tls_init + tls_zero + (int)tls_static);
}

extern void maybe(void) __attribute__((weak));
__attribute__((weak)) int weak_def(void) { return 7; }

int main(void)
{
	char buf[64];
	struct r3 a = { { 1, 2, 3 } };
	struct r8 b = { 4, 5 };
	struct r12 c = { 6, 7, 8 };
	struct r40 d = { { 9, 0, 0, 0, 10 } };
	long double e = 11;
	__int128 f = 12;
	int v[16];
	u32 tid = 0, code = 0;
	void *h;

	printf("ctors %d: %d %d %d\n", norder, order[0], order[1], order[2]);
	/* the shadow space: snprintf spills its register arguments there */
	snprintf(buf, sizeof buf, "%d %s %.2f %lld %c", 1, "two", 3.0, 4LL, '5');
	printf("shadow [%s]\n", buf);
	printf("byref %d", byref(a, b, c, d, e, f));
	printf(" kept %d %d %d %g %g %d\n", a.c[0], b.a, c.a, d.d[0], (double)e, (int)f);
	printf("varargs %g\n", vsum(10, 1.5, 2, c, (long double)3.25, b, 4.5, 6, c, (long double)0.25, b));
	for (int i = 0; i < 16; i++)
		v[i] = (i * 7) % 16;
	{
		double x = b.a + 0.5, y = x * 3;
		long long u = c.a + 100, w = u * u;

		qsort(v, 16, sizeof *v, cmp);
		printf("qsort %d %d %d, kept %g %g %lld %lld\n", v[0], v[7], v[15], x, y, u, w);
	}
	printf("pages %d\n", deep_frame(3));
	h = CreateThread(0, 0, thread, (void *)(u64)8, 0, &tid);
	WaitForSingleObject(h, 0xffffffffu);
	{
		/* GetExitCodeThread's value through the thread's return */
		__declspec(dllimport) int GetExitCodeThread(void *, u32 *);

		GetExitCodeThread(h, &code);
	}
	CloseHandle(h);
	tls_init++;
	printf("tls main %d %d %lld, thread %u, %d\n", tls_init, tls_zero, tls_static, code, &tls_init != 0);
	printf("weak %d %d\n", maybe ? 1 : 0, weak_def());
	printf("refptr %d\n", stdout != 0);
	return 0;
}
