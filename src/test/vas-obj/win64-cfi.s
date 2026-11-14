	.text
	.globl	f1
f1:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	pushq	%rbx
	.cfi_rel_offset %rbx, 0
	.cfi_remember_state
	popq	%rbx
	.cfi_restore %rbx
	popq	%rbp
	.cfi_def_cfa %rsp, 8
	ret
	.cfi_restore_state
	.cfi_endproc
	.section	.text$f2,"xr",discard,f2
f2:
	.cfi_startproc
	.cfi_adjust_cfa_offset 8
	ret
	.cfi_endproc
