/*
 * ___chkstk_ms: the stack probe a function whose frame is a page (4 KiB) or
 * larger calls before it allocates the frame (Windows x64, MinGW flavour):
 *
 *	movl	$SIZE, %eax
 *	callq	___chkstk_ms
 *	subq	%rax, %rsp
 *
 * Windows commits a thread's stack one guard page at a time, so the pages
 * between the caller's %rsp and %rsp - SIZE must be touched in order, from
 * the top down, before any of them is used.  This routine touches one
 * address in every page of that range, ending at %rsp - SIZE itself, and
 * changes nothing else: every register, %rax included (the caller
 * subtracts it next), is as it was; only the flags are clobbered.  It does
 * not move the caller's %rsp (that is MSVC's __chkstk, not this one).
 *
 * At entry the return address is at (%rsp), so the caller's %rsp is
 * %rsp + 8; after the two pushes below it is %rsp + 24.
 */
	.text
	.globl	___chkstk_ms
	.def	___chkstk_ms; .scl 2; .type 32; .endef
	.p2align 4
___chkstk_ms:
	.seh_proc ___chkstk_ms
	pushq	%rcx
	.seh_pushreg %rcx
	pushq	%rax
	.seh_pushreg %rax
	.seh_endprologue
	leaq	24(%rsp), %rcx		/* the caller's %rsp: the top of the range */
	cmpq	$0x1000, %rax
	jb	2f
1:	subq	$0x1000, %rcx		/* one page further down, touched */
	orq	$0, (%rcx)
	subq	$0x1000, %rax
	cmpq	$0x1000, %rax
	ja	1b
2:	subq	%rax, %rcx		/* the last partial page: %rsp - SIZE itself */
	orq	$0, (%rcx)
	popq	%rax
	popq	%rcx
	retq
	.seh_endproc
