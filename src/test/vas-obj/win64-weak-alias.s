	.def	fn;
	.scl	2;
	.type	32;
	.endef
	.text
	.globl	fn
fn:
	call	ext@PLT
	movq	ext@GOTPCREL(%rip), %rax
	jmp	fn
	ret
glab:
	ret
	.def	.Lstat;
	.scl	3;
	.endef
.Lstat:
	ret
	.data
	.long	fn-glab
	.long	glab-fn
	.quad	.Lstat
	.weak	wa
	.set	wa, ext3
	.weak	wb
	.set	wb, glab
	.quad	wa
	.quad	wb
	.long	ext@PLT
