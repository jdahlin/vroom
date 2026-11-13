	.def	@feat.00;
	.scl	3;
	.type	0;
	.endef
	.globl	@feat.00
@feat.00 = 0
	.att_syntax
	.file	"ctor_priority.c"
	.def	last;
	.scl	3;
	.type	32;
	.endef
	.text
	.p2align	4                               # -- Begin function last
last:                                   # @last
# %bb.0:
	movl	n(%rip), %eax
	movl	%eax, %ecx
	addl	$1, %ecx
	movl	%ecx, n(%rip)
	movslq	%eax, %rcx
	leaq	order(%rip), %rax
	movl	$3, (%rax,%rcx,4)
	retq
                                        # -- End function
	.def	second;
	.scl	3;
	.type	32;
	.endef
	.p2align	4                               # -- Begin function second
second:                                 # @second
# %bb.0:
	movl	n(%rip), %eax
	movl	%eax, %ecx
	addl	$1, %ecx
	movl	%ecx, n(%rip)
	movslq	%eax, %rcx
	leaq	order(%rip), %rax
	movl	$2, (%rax,%rcx,4)
	retq
                                        # -- End function
	.def	first;
	.scl	3;
	.type	32;
	.endef
	.p2align	4                               # -- Begin function first
first:                                  # @first
# %bb.0:
	movl	n(%rip), %eax
	movl	%eax, %ecx
	addl	$1, %ecx
	movl	%ecx, n(%rip)
	movslq	%eax, %rcx
	leaq	order(%rip), %rax
	movl	$1, (%rax,%rcx,4)
	retq
                                        # -- End function
	.def	main;
	.scl	2;
	.type	32;
	.endef
	.globl	main                            # -- Begin function main
	.p2align	4
main:                                   # @main
.seh_proc main
# %bb.0:
	pushq	%rbp
	.seh_pushreg %rbp
	subq	$48, %rsp
	.seh_stackalloc 48
	leaq	48(%rsp), %rbp
	.seh_setframe %rbp, 48
	.seh_endprologue
	callq	__main
	movl	$0, -4(%rbp)
	movl	n(%rip), %edx
	movl	order(%rip), %r8d
	movl	order+4(%rip), %r9d
	movl	order+8(%rip), %ecx
	movq	%rsp, %rax
	movl	%ecx, 32(%rax)
	leaq	.L.str(%rip), %rcx
	callq	printf
	xorl	%eax, %eax
                                        # kill: def $al killed $al killed $eax
	cmpl	$3, n(%rip)
	movb	%al, -5(%rbp)                   # 1-byte Spill
	jne	.LBB3_4
# %bb.1:
	xorl	%eax, %eax
                                        # kill: def $al killed $al killed $eax
	cmpl	$1, order(%rip)
	movb	%al, -5(%rbp)                   # 1-byte Spill
	jne	.LBB3_4
# %bb.2:
	xorl	%eax, %eax
                                        # kill: def $al killed $al killed $eax
	cmpl	$2, order+4(%rip)
	movb	%al, -5(%rbp)                   # 1-byte Spill
	jne	.LBB3_4
# %bb.3:
	cmpl	$3, order+8(%rip)
	sete	%al
	movb	%al, -5(%rbp)                   # 1-byte Spill
.LBB3_4:
	movb	-5(%rbp), %al                   # 1-byte Reload
	xorb	$-1, %al
	andb	$1, %al
	movzbl	%al, %eax
	.seh_startepilogue
	addq	$48, %rsp
	popq	%rbp
	.seh_endepilogue
	retq
	.seh_endproc
                                        # -- End function
	.lcomm	order,16,16                     # @order
	.lcomm	n,4,4                           # @n
	.section	.rdata,"dr"
.L.str:                                 # @.str
	.asciz	"%d: %d %d %d\n"

	.section	.ctors,"dw",unique,0
	.p2align	3, 0x0
	.quad	last
	.section	.ctors.65335,"dw",unique,0
	.p2align	3, 0x0
	.quad	second
	.section	.ctors.65434,"dw",unique,0
	.p2align	3, 0x0
	.quad	first
	.section	.debug$S,"dr"
	.p2align	2, 0x0
	.long	4                               # Debug section magic
	.long	241
	.long	.Ltmp1-.Ltmp0                   # Subsection size
.Ltmp0:
	.short	.Ltmp3-.Ltmp2                   # Record length
.Ltmp2:
	.short	4353                            # Record kind: S_OBJNAME
	.long	0                               # Signature
	.byte	0                               # Object name
	.p2align	2, 0x0
.Ltmp3:
	.short	.Ltmp5-.Ltmp4                   # Record length
.Ltmp4:
	.short	4412                            # Record kind: S_COMPILE3
	.long	0                               # Flags and language
	.short	208                             # CPUType
	.short	23                              # Frontend version
	.short	1
	.short	6
	.short	0
	.short	23016                           # Backend version
	.short	0
	.short	0
	.short	0
	.asciz	"Ubuntu clang version 23.1.6 (++20261108033809+c47d7823685a-1~exp1~20261108153913.94)" # Null-terminated compiler version string
	.p2align	2, 0x0
.Ltmp5:
.Ltmp1:
	.p2align	2, 0x0
	.addrsig
	.addrsig_sym last
	.addrsig_sym second
	.addrsig_sym first
	.addrsig_sym printf
	.addrsig_sym order
	.addrsig_sym n
