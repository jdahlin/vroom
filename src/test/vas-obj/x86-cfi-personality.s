	.text
	.globl	vcc_try
	.type	vcc_try,@function
vcc_try:
	.cfi_startproc
	.cfi_personality 0x9b, .Lvcc_pers
	.cfi_lsda 0x1b, .Lvcc_lsda_0
	pushq	%rbx
	.cfi_def_cfa_offset 16
	.cfi_offset %rbx, -16
	movq	%rdx, %rbx
	movq	%rdi, %rax
	movq	%rsi, %rdi
.Lvcc_cs_0:
	callq	*%rax
.Lvcc_ce_0:
	popq	%rbx
	.cfi_def_cfa_offset 8
	retq
.Lvcc_lp_0:
	.cfi_def_cfa_offset 16
	movq	%rax, (%rbx)
	movl	%edx, %eax
	negl	%eax
	popq	%rbx
	.cfi_def_cfa_offset 8
	retq
	.cfi_endproc
.Lvcc_end_0:
	.size	vcc_try, .Lvcc_end_0-vcc_try
	.section	.gcc_except_table,"a",@progbits
.Lvcc_lsda_0:
	.byte	0xff
	.byte	0x9b
	.uleb128 .Lvcc_tt_0-.Lvcc_ttb_0
.Lvcc_ttb_0:
	.byte	0x01
	.uleb128 .Lvcc_cse_0-.Lvcc_csb_0
.Lvcc_csb_0:
	.uleb128 .Lvcc_cs_0-vcc_try
	.uleb128 .Lvcc_ce_0-.Lvcc_cs_0
	.uleb128 .Lvcc_lp_0-vcc_try
	.uleb128 1
.Lvcc_cse_0:
	.byte	1
	.byte	1
	.byte	2
	.byte	0
	.long	0
	.long	.Lvcc_ti_0_1-.
.Lvcc_tt_0:
	.section	.data.rel.ro,"aw",@progbits
	.p2align	3
.Lvcc_pers:
	.quad	__gxx_personality_v0
.Lvcc_ti_0_1:
	.quad	_ZTIi
	.text
