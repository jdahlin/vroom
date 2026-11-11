/* An asm that clobbers every caller-saved register (the kernel's
   call_on_stack): its "r" inputs go to callee-saved registers, which the
   prologue saves (x86-64 ran out of temporaries). */
#include <stdio.h>

long got;
void handler(long *r, unsigned long a, int *info) { got = *r + (long)a + *info; }

#ifdef __x86_64__
static void call3(long *regs, unsigned long address, int *info)
{
	asm volatile("movq %[a1], %%rdi\n\tmovq %[a2], %%rsi\n\tmovq %[a3], %%rdx\n\tcall %P[fn]"
		     :
		     : [fn] "i" (handler), [a1] "r" (regs), [a2] "r" (address), [a3] "r" (info)
		     : "cc", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory");
}
#else
static void call3(long *regs, unsigned long address, int *info) { handler(regs, address, info); }
#endif

int main(void)
{
	long r = 100, keep = 7;
	int info = 3;

	call3(&r, 20, &info);
	printf("%ld %ld\n", got, keep);
	return 0;
}
