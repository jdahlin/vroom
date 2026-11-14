/*
 * stack_probe_check(size, before, after): every general register set to a
 * distinct pattern, ___chkstk_ms called with %rax = size, then every
 * register stored again (stack-probe.c compares them).  Win64: size in
 * %rcx, before in %rdx, after in %r8.  The callee-saved registers are saved
 * and restored around the test; the pointers are kept on the stack.
 */
	.text
	.globl	stack_probe_check
	.def	stack_probe_check; .scl 2; .type 32; .endef
	.p2align 4
stack_probe_check:
	.seh_proc stack_probe_check
	pushq	%rbp
	.seh_pushreg %rbp
	pushq	%rbx
	.seh_pushreg %rbx
	pushq	%rsi
	.seh_pushreg %rsi
	pushq	%rdi
	.seh_pushreg %rdi
	pushq	%r12
	.seh_pushreg %r12
	pushq	%r13
	.seh_pushreg %r13
	pushq	%r14
	.seh_pushreg %r14
	pushq	%r15
	.seh_pushreg %r15
	subq	$56, %rsp
	.seh_stackalloc 56
	.seh_endprologue
	movq	%rdx, 32(%rsp)		/* before */
	movq	%r8, 40(%rsp)		/* after */
	movq	%rcx, %rax		/* the size */
	movabsq	$0x1111111111111101, %rcx
	movabsq	$0x2222222222222202, %rdx
	movabsq	$0x3333333333333303, %rbx
	movabsq	$0x5555555555555505, %rbp
	movabsq	$0x6666666666666606, %rsi
	movabsq	$0x7777777777777707, %rdi
	movabsq	$0x8888888888888808, %r8
	movabsq	$0x9999999999999909, %r9
	movabsq	$0xaaaaaaaaaaaaaa0a, %r10
	movabsq	$0xbbbbbbbbbbbbbb0b, %r11
	movabsq	$0xcccccccccccccc0c, %r12
	movabsq	$0xdddddddddddddd0d, %r13
	movabsq	$0xeeeeeeeeeeeeee0e, %r14
	movabsq	$0xffffffffffffff0f, %r15
	/* store "before" through a register we then restore: r11 holds the pointer briefly */
	pushq	%r11
	movq	40(%rsp), %r11		/* before (32 + 8 for the push) */
	movq	%rax, 0(%r11)
	movq	%rcx, 8(%r11)
	movq	%rdx, 16(%r11)
	movq	%rbx, 24(%r11)
	leaq	8(%rsp), %rcx
	movq	%rcx, 32(%r11)		/* rsp as it is at the call */
	movabsq	$0x1111111111111101, %rcx
	movq	%rbp, 40(%r11)
	movq	%rsi, 48(%r11)
	movq	%rdi, 56(%r11)
	movq	%r8, 64(%r11)
	movq	%r9, 72(%r11)
	movq	%r10, 80(%r11)
	movabsq	$0xbbbbbbbbbbbbbb0b, %r12
	movq	%r12, 88(%r11)
	movabsq	$0xcccccccccccccc0c, %r12
	movq	%r12, 96(%r11)
	movq	%r13, 104(%r11)
	movq	%r14, 112(%r11)
	movq	%r15, 120(%r11)
	popq	%r11
	callq	___chkstk_ms
	pushq	%r11
	movq	48(%rsp), %r11		/* after (40 + 8) */
	movq	%rax, 0(%r11)
	movq	%rcx, 8(%r11)
	movq	%rdx, 16(%r11)
	movq	%rbx, 24(%r11)
	leaq	8(%rsp), %rcx
	movq	%rcx, 32(%r11)
	movq	%rbp, 40(%r11)
	movq	%rsi, 48(%r11)
	movq	%rdi, 56(%r11)
	movq	%r8, 64(%r11)
	movq	%r9, 72(%r11)
	movq	%r10, 80(%r11)
	popq	%rcx			/* r11 as ___chkstk_ms left it */
	movq	%rcx, 88(%r11)
	movq	%r12, 96(%r11)
	movq	%r13, 104(%r11)
	movq	%r14, 112(%r11)
	movq	%r15, 120(%r11)
	addq	$56, %rsp
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rdi
	popq	%rsi
	popq	%rbx
	popq	%rbp
	retq
	.seh_endproc

/*
 * stack_probe_on(top, size): ___chkstk_ms with %rax = size and %rsp = top
 * (a region of the caller's, write-watched), then back on the real stack.
 * Nothing in between can fault or unwind.
 */
	.globl	stack_probe_on
	.def	stack_probe_on; .scl 2; .type 32; .endef
	.p2align 4
stack_probe_on:
	.seh_proc stack_probe_on
	pushq	%rbx
	.seh_pushreg %rbx
	.seh_endprologue
	movq	%rsp, %rbx
	movq	%rcx, %rsp
	movq	%rdx, %rax
	callq	___chkstk_ms
	movq	%rbx, %rsp
	popq	%rbx
	retq
	.seh_endproc
