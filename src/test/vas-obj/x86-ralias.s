# x86: name = %reg aliases (crypto code); registers compared in .if (UNWIND_HINT)
msg = %rsi
len = %rdx
	.macro chk base
	.if \base == %rsp
	nop
	.else
	int3
	.endif
	.endm
	movq (msg), %rax
	addq $16, msg
	leaq 8(msg,len,4), %rcx
	chk %rsp
	chk %rbp
	movq msg, len
