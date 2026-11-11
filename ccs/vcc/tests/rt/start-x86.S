/* x86-64 Linux start for the bare runtime (rt.c): static TLS (variant 2: the thread
   pointer at the end of the block, %fs:0 holding itself), main through __rt_main,
   exit_group; setjmp/longjmp.  Linked with link-x86.ld, by lld or vlink. */
	.section .text.start, "ax"
	.globl	_start
_start:
	xorl	%ebp, %ebp
	andq	$-16, %rsp
	/* tp: 16 KB into the 4 KB-aligned area; the block below it (its tpoff) */
	leaq	tls_area+16384(%rip), %rbx
	movq	%rbx, (%rbx)			/* the TCB's self pointer (%fs:0) */
	movq	%rbx, %rdi
	subq	$__tls_size, %rdi		/* the block */
	leaq	__tdata_start(%rip), %rsi	/* its initial image */
	movq	$__tdata_size, %rcx
	rep movsb
	/* .tbss: already zero (tls_area is .bss) */
	movl	$158, %eax			/* arch_prctl(ARCH_SET_FS, tp) */
	movl	$0x1002, %edi
	movq	%rbx, %rsi
	syscall
	call	__rt_main
	movl	%eax, %edi
	.globl	_exit
_exit:
	pushq	%rdi
	call	__rt_flush
	popq	%rdi
	movl	$231, %eax			/* exit_group */
	syscall
	hlt

	.globl	setjmp
	.globl	_setjmp
setjmp:
_setjmp:
	movq	%rbx, 0(%rdi)
	movq	%rbp, 8(%rdi)
	movq	%r12, 16(%rdi)
	movq	%r13, 24(%rdi)
	movq	%r14, 32(%rdi)
	movq	%r15, 40(%rdi)
	leaq	8(%rsp), %rdx			/* rsp after the return */
	movq	%rdx, 48(%rdi)
	movq	(%rsp), %rdx			/* the return address */
	movq	%rdx, 56(%rdi)
	xorl	%eax, %eax
	ret

	.globl	longjmp
	.globl	_longjmp
longjmp:
_longjmp:
	movq	0(%rdi), %rbx
	movq	8(%rdi), %rbp
	movq	16(%rdi), %r12
	movq	24(%rdi), %r13
	movq	32(%rdi), %r14
	movq	40(%rdi), %r15
	movq	48(%rdi), %rsp
	movl	%esi, %eax
	testl	%eax, %eax
	jnz	1f
	incl	%eax
1:	jmpq	*56(%rdi)

	.bss
	.p2align 12
tls_area:
	.space	64 << 10
	.section .note.GNU-stack, "", @progbits
