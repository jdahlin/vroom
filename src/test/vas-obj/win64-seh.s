	.text
	.def	f1;
	.scl	2;
	.type	32;
	.endef
	.globl	f1
	.p2align	4
f1:
.seh_proc f1
	pushq	%rbp
	.seh_pushreg %rbp
	pushq	%rbx
	.seh_pushreg 3
	subq	$40, %rsp
	.seh_stackalloc 40
	leaq	32(%rsp), %rbp
	.seh_setframe %rbp, 32
	movq	%rsi, 16(%rsp)
	.seh_savereg %rsi, 16
	movq	%rdi, 524288(%rsp)
	.seh_savereg %rdi, 524288
	movaps	%xmm6, 0(%rsp)
	.seh_savexmm %xmm6, 0
	movaps	%xmm7, 1048576(%rsp)
	.seh_savexmm 7, 1048576
	.seh_endprologue
	nop
	.seh_startepilogue
	addq	$40, %rsp
	popq	%rbx
	popq	%rbp
	.seh_endepilogue
	retq
	.seh_endproc

	.def	f2;
	.scl	2;
	.type	32;
	.endef
	.globl	f2
f2:
.seh_proc f2
	.seh_endprologue
	retq
	.seh_endproc

f3:
.seh_proc f3
	.seh_pushframe @code
	subq	$600000, %rsp
	.seh_stackalloc 600000
	subq	$8, %rsp
	.seh_stackalloc 8
	subq	$128, %rsp
	.seh_stackalloc 128
	subq	$136, %rsp
	.seh_stackalloc 136
	.seh_endprologue
	retq
	.seh_handler __C_specific_handler, @except, @unwind
	.seh_handlerdata
	.long	1
	.long	f3@IMGREL
	.text
	.seh_endproc

	.section	.text$f4,"xr",discard,f4
	.def	f4;
	.scl	2;
	.type	32;
	.endef
	.globl	f4
f4:
.seh_proc f4
	pushq	%rsi
	.seh_pushreg %rsi
	.seh_endprologue
	popq	%rsi
	retq
	.seh_handler h2, @unwind
	.seh_endproc

	.section	.text.startup,"xr"
f5:
.seh_proc f5
	pushq	%rsi
	.seh_pushreg %rsi
	.seh_endprologue
	retq
	.seh_endproc

	.text
f6:
.seh_proc f6
	pushq	%rsi
	.seh_pushreg %rsi
	retq
	.seh_endproc
	.section	.text$f4,"xr",discard,f4
f7:
.seh_proc f7
	retq
	.seh_endproc
