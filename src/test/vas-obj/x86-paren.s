# x86: .align is a byte count; a displacement may start with a parenthesis
	.text
	lea (2f)(%rip), %r10
	.align 16
2:	movq (8+8)*2(%rax), %rbx
	movq (16), %rcx
