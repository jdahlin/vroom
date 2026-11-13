/* Windows x64 (PLAN-win64.md WIN3, validation point 4): a vectored exception handler unwinds out
   of a function that faulted (RtlVirtualUnwind from the fault's context, in the middle of its
   body) and resumes its caller as if it had returned: the caller's callee-saved registers, rsp
   and rbp must come back from the faulting frame's unwind data. */
#include <stdio.h>
#include "winapi.h"

#define NI __attribute__((noinline))

static volatile int sink;
static int faults;

static long handler(exception_pointers *ep)
{
	context *c = ep->ctx;
	u64 pc = CTX(c, CTX_RIP), base, frame;
	void *data;
	runtime_function *f;

	if (ep->rec->code != 0xc0000005u)
		return 0;	/* EXCEPTION_CONTINUE_SEARCH */
	f = RtlLookupFunctionEntry(pc, &base, 0);
	if (f) {
		RtlVirtualUnwind(0, base, pc, f, c, &data, &frame, 0);
	} else {	/* a leaf: the return address on top */
		CTX(c, CTX_RIP) = *(u64 *)CTX(c, CTX_RSP);
		CTX(c, CTX_RSP) += 8;
	}
	CTX(c, CTX_RAX) = 1000 + (u64)faults++;	/* what the faulting function "returned" */
	return -1;	/* EXCEPTION_CONTINUE_EXECUTION */
}

NI int fault_leaf(volatile int *p) { return *p + 1; }

NI int fault_saves(volatile int *p, int k)
{
	volatile int a[10];

	__asm__ volatile("xorl %%ebx, %%ebx; xorl %%esi, %%esi; xorl %%edi, %%edi; xorl %%r12d, %%r12d; xorps %%xmm6, %%xmm6;"
			 "xorps %%xmm7, %%xmm7; xorps %%xmm9, %%xmm9; xorps %%xmm15, %%xmm15"
			 ::: "rbx", "rsi", "rdi", "r12", "xmm6", "xmm7", "xmm9", "xmm15");
	a[k] = k;
	return *p + a[k];
}

NI int fault_pages(volatile int *p, int k)
{
	volatile char big[9000];

	big[8999] = (char)k;
	big[0] = (char)k;
	return *p + big[0] + big[8999];
}

NI int fault_dynamic(volatile int *p, int k)
{
	volatile char *q = __builtin_alloca((unsigned)k * 64 + 16);

	q[0] = (char)k;
	return *p + q[0];
}

/* the caller holds values in registers across the faulting call */
NI double caller(int (*f)(volatile int *, int), volatile int *p)
{
	double x = sink + 1.5, y = x * 3;
	long long u = sink * 3 + 7, v = u * u;
	int r = f(p, 3);

	return r + x + y + (double)(u + v);
}

static int leaf2(volatile int *p, int k) { (void)k; return fault_leaf(p); }

int main(void)
{
	void *h = AddVectoredExceptionHandler(1, handler);
	volatile int *null = 0;

	sink = 1;
	printf("leaf %g\n", caller(leaf2, null));
	printf("saves %g\n", caller(fault_saves, null));
	printf("pages %g\n", caller(fault_pages, null));
	printf("dynamic %g\n", caller(fault_dynamic, null));
	printf("faults %d\n", faults);
	RemoveVectoredExceptionHandler(h);
	return 0;
}
