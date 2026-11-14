/* Windows x64 (PLAN-win64.md WIN3, validation point 4): the unwinder walks out of every prologue
   shape vcc makes (no saves, GPR saves, XMM saves, a frame of pages (___chkstk_ms), alloca, a
   realigned frame, a variadic function's homed registers), by RtlVirtualUnwind from
   RtlCaptureContext and by RtlCaptureStackBackTrace.  Each frame is named by the
   RUNTIME_FUNCTION its return address is in, so a wrong .pdata/.xdata (or a missing one) shows. */
#include <stdio.h>
#include <string.h>
#include "winapi.h"

#define NI __attribute__((noinline))

static const char *names[16];
static u64 starts[16];
static int nfn;
static volatile int sink;

#define REG(f) (names[nfn] = #f, starts[nfn++] = (u64)(void *)f)

static const char *name_of(u64 start)
{
	for (int i = 0; i < nfn; i++)
		if (starts[i] == start)
			return names[i];
	return start ? "?" : "leaf";
}

NI int walk(int depth)
{
	context c;
	void *frames[32];
	unsigned short n;

	RtlCaptureContext(&c);
	printf("unwind:");
	for (int i = 0; i < 16; i++) {
		u64 pc = CTX(&c, CTX_RIP), base, frame;
		void *data;
		runtime_function *f = RtlLookupFunctionEntry(pc, &base, 0);
		const char *nm = name_of(f ? base + f->begin : 0);

		printf(" %s", nm);
		if (!strcmp(nm, "main") || !strcmp(nm, "?"))
			break;
		if (!f) {	/* a leaf: the return address on top */
			CTX(&c, CTX_RIP) = *(u64 *)CTX(&c, CTX_RSP);
			CTX(&c, CTX_RSP) += 8;
			continue;
		}
		RtlVirtualUnwind(0, base, pc, f, &c, &data, &frame, 0);
	}
	printf("\nbacktrace:");
	n = RtlCaptureStackBackTrace(0, 32, frames, 0);
	for (unsigned i = 0; i < n; i++) {
		const char *nm = name_of(fn_start((u64)frames[i]));

		printf(" %s", nm);
		if (!strcmp(nm, "main") || !strcmp(nm, "?"))
			break;
	}
	printf("\n");
	return depth;
}

NI int plain(int d) { return walk(d) + 1; }

NI int gprs(int d)
{
	__asm__ volatile("" ::: "rbx", "rsi", "rdi", "r12", "r13", "r14", "r15");
	return plain(d) + 2;
}

NI int xmms(int d)
{
	__asm__ volatile("" ::: "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
	return gprs(d) + 3;
}

NI int pages(int d)
{
	volatile char buf[20000];

	buf[19999] = (char)d;
	buf[0] = (char)d;
	return xmms(d) + buf[0] + buf[19999];
}

NI int dynamic(int d)
{
	volatile char *p = __builtin_alloca((unsigned)d * 1000 + 100);

	p[0] = 1;
	sink = p[0];
	return pages(d) + p[0];
}

NI int aligned(int d)
{
	_Alignas(64) volatile char a[64];

	a[0] = (char)d;
	sink = (int)((u64)&a[0] % 64);
	return dynamic(d) + a[0] + sink;
}

NI int vararg(int n, ...)
{
	__builtin_va_list ap;
	int d;

	__builtin_va_start(ap, n);
	d = __builtin_va_arg(ap, int);
	__builtin_va_end(ap);
	return aligned(d) + n;
}

int main(void)
{
	REG(walk); REG(plain); REG(gprs); REG(xmms); REG(pages); REG(dynamic); REG(aligned); REG(vararg); REG(main);
	printf("%d\n", vararg(1, 2));
	return 0;
}
