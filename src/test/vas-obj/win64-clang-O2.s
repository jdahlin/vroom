	.def	@feat.00;
	.scl	3;
	.type	0;
	.endef
	.globl	@feat.00
@feat.00 = 0
	.att_syntax
	.file	"a.c"
	.def	wk;
	.scl	2;
	.type	32;
	.endef
	.text
	.weak	wk                              # -- Begin function wk
	.p2align	4
wk:                                     # @wk
# %bb.0:
	movl	$1, %eax
	retq
                                        # -- End function
	.def	dd;
	.scl	2;
	.type	32;
	.endef
	.section	.rdata,"dr"
	.p2align	3, 0x0                          # -- Begin function dd
.LCPI1_0:
	.quad	0x4004000000000000              # double 2.5
	.text
	.globl	dd
	.p2align	4
dd:                                     # @dd
# %bb.0:
	mulsd	.LCPI1_0(%rip), %xmm0
	retq
                                        # -- End function
	.def	big;
	.scl	2;
	.type	32;
	.endef
	.globl	big                             # -- Begin function big
	.p2align	4
big:                                    # @big
.seh_proc big
# %bb.0:
	movl	$8200, %eax                     # imm = 0x2008
	callq	___chkstk_ms
	subq	%rax, %rsp
	.seh_stackalloc 8200
	.seh_endprologue
	movslq	%ecx, %rax
	movb	$1, (%rsp,%rax)
	movsbl	(%rsp), %eax
	movq	.refptr.ext(%rip), %rcx
	addl	(%rcx), %eax
	movl	_tls_index(%rip), %ecx
	movq	%gs:88, %rdx
	movq	(%rdx,%rcx,8), %rcx
	addl	tl@SECREL32(%rcx), %eax
	.seh_startepilogue
	addq	$8200, %rsp                     # imm = 0x2008
	.seh_endepilogue
	retq
	.seh_endproc
                                        # -- End function
	.def	f;
	.scl	2;
	.type	32;
	.endef
	.globl	f                               # -- Begin function f
	.p2align	4
f:                                      # @f
.seh_proc f
# %bb.0:
	pushq	%rsi
	.seh_pushreg %rsi
	subq	$48, %rsp
	.seh_stackalloc 48
	.seh_endprologue
	movq	%rdx, %rsi
	movq	%rdx, 72(%rsp)
	movq	%r8, 80(%rsp)
	movq	%r9, 88(%rsp)
	leaq	80(%rsp), %rax
	movq	%rax, 40(%rsp)
	addl	%ecx, %esi
	addl	g(%rip), %esi
	callq	wk
                                        # kill: def $eax killed $eax def $rax
	addl	%esi, %eax
	incl	%eax
	.seh_startepilogue
	addq	$48, %rsp
	popq	%rsi
	.seh_endepilogue
	retq
	.seh_endproc
                                        # -- End function
	.def	sw;
	.scl	2;
	.type	32;
	.endef
	.globl	sw                              # -- Begin function sw
	.p2align	4
sw:                                     # @sw
# %bb.0:
	xorl	%eax, %eax
	cmpl	$4, %ecx
	ja	.LBB4_2
# %bb.1:
	movl	%ecx, %eax
	leaq	.Lswitch.table.sw(%rip), %rcx
	movzbl	(%rax,%rcx), %eax
.LBB4_2:
	retq
                                        # -- End function
	.def	xm;
	.scl	2;
	.type	32;
	.endef
	.globl	xm                              # -- Begin function xm
	.p2align	4
xm:                                     # @xm
.seh_proc xm
# %bb.0:
	pushq	%rsi
	.seh_pushreg %rsi
	subq	$240, %rsp
	.seh_stackalloc 240
	movaps	%xmm15, 224(%rsp)               # 16-byte Spill
	.seh_savexmm %xmm15, 224
	movaps	%xmm14, 208(%rsp)               # 16-byte Spill
	.seh_savexmm %xmm14, 208
	movaps	%xmm13, 192(%rsp)               # 16-byte Spill
	.seh_savexmm %xmm13, 192
	movaps	%xmm12, 176(%rsp)               # 16-byte Spill
	.seh_savexmm %xmm12, 176
	movaps	%xmm11, 160(%rsp)               # 16-byte Spill
	.seh_savexmm %xmm11, 160
	movaps	%xmm10, 144(%rsp)               # 16-byte Spill
	.seh_savexmm %xmm10, 144
	movaps	%xmm9, 128(%rsp)                # 16-byte Spill
	.seh_savexmm %xmm9, 128
	movaps	%xmm8, 112(%rsp)                # 16-byte Spill
	.seh_savexmm %xmm8, 112
	movaps	%xmm7, 96(%rsp)                 # 16-byte Spill
	.seh_savexmm %xmm7, 96
	movaps	%xmm6, 80(%rsp)                 # 16-byte Spill
	.seh_savexmm %xmm6, 80
	.seh_endprologue
	movq	%rcx, %rsi
	movsd	(%rcx), %xmm14                  # xmm14 = mem[0],zero
	movsd	8(%rcx), %xmm12                 # xmm12 = mem[0],zero
	movsd	16(%rcx), %xmm13                # xmm13 = mem[0],zero
	movsd	24(%rcx), %xmm10                # xmm10 = mem[0],zero
	movsd	32(%rcx), %xmm9                 # xmm9 = mem[0],zero
	movsd	40(%rcx), %xmm7                 # xmm7 = mem[0],zero
	movsd	48(%rcx), %xmm6                 # xmm6 = mem[0],zero
	movsd	56(%rcx), %xmm11                # xmm11 = mem[0],zero
	movsd	72(%rcx), %xmm0                 # xmm0 = mem[0],zero
	movsd	%xmm0, 48(%rsp)                 # 8-byte Spill
	movsd	80(%rcx), %xmm0                 # xmm0 = mem[0],zero
	movsd	%xmm0, 40(%rsp)                 # 8-byte Spill
	movsd	88(%rcx), %xmm0                 # xmm0 = mem[0],zero
	movsd	%xmm0, 64(%rsp)                 # 8-byte Spill
	movsd	96(%rcx), %xmm0                 # xmm0 = mem[0],zero
	movsd	%xmm0, 56(%rsp)                 # 8-byte Spill
	movsd	104(%rcx), %xmm8                # xmm8 = mem[0],zero
	movsd	112(%rcx), %xmm0                # xmm0 = mem[0],zero
	movsd	%xmm0, 72(%rsp)                 # 8-byte Spill
	movsd	120(%rcx), %xmm15               # xmm15 = mem[0],zero
	mulsd	64(%rcx), %xmm11
	callq	ff
	movapd	%xmm12, %xmm0
	mulsd	%xmm13, %xmm0
	addsd	%xmm14, %xmm0
	movapd	%xmm10, %xmm1
	mulsd	%xmm9, %xmm1
	addsd	%xmm0, %xmm1
	movapd	%xmm7, %xmm0
	mulsd	%xmm6, %xmm0
	addsd	%xmm1, %xmm0
	addsd	%xmm11, %xmm0
	movsd	48(%rsp), %xmm2                 # 8-byte Reload
                                        # xmm2 = mem[0],zero
	movapd	%xmm2, %xmm1
	movsd	40(%rsp), %xmm3                 # 8-byte Reload
                                        # xmm3 = mem[0],zero
	mulsd	%xmm3, %xmm1
	addsd	%xmm0, %xmm1
	movsd	64(%rsp), %xmm4                 # 8-byte Reload
                                        # xmm4 = mem[0],zero
	movapd	%xmm4, %xmm0
	movsd	56(%rsp), %xmm5                 # 8-byte Reload
                                        # xmm5 = mem[0],zero
	mulsd	%xmm5, %xmm0
	addsd	%xmm1, %xmm0
	mulsd	%xmm8, %xmm13
	movsd	72(%rsp), %xmm1                 # 8-byte Reload
                                        # xmm1 = mem[0],zero
	mulsd	%xmm1, %xmm8
	addsd	%xmm0, %xmm8
	addsd	%xmm15, %xmm8
	movsd	%xmm8, (%rsi)
	mulsd	%xmm15, %xmm14
	movsd	%xmm14, 8(%rsi)
	mulsd	%xmm1, %xmm12
	movsd	%xmm12, 16(%rsi)
	movsd	%xmm13, 24(%rsi)
	mulsd	%xmm5, %xmm10
	movsd	%xmm10, 32(%rsi)
	mulsd	%xmm4, %xmm9
	movsd	%xmm9, 40(%rsi)
	mulsd	%xmm3, %xmm7
	movsd	%xmm7, 48(%rsi)
	mulsd	%xmm2, %xmm6
	movsd	%xmm6, 56(%rsi)
	movaps	80(%rsp), %xmm6                 # 16-byte Reload
	movaps	96(%rsp), %xmm7                 # 16-byte Reload
	movaps	112(%rsp), %xmm8                # 16-byte Reload
	movaps	128(%rsp), %xmm9                # 16-byte Reload
	movaps	144(%rsp), %xmm10               # 16-byte Reload
	movaps	160(%rsp), %xmm11               # 16-byte Reload
	movaps	176(%rsp), %xmm12               # 16-byte Reload
	movaps	192(%rsp), %xmm13               # 16-byte Reload
	movaps	208(%rsp), %xmm14               # 16-byte Reload
	movaps	224(%rsp), %xmm15               # 16-byte Reload
	.seh_startepilogue
	addq	$240, %rsp
	popq	%rsi
	.seh_endepilogue
	retq
	.seh_endproc
                                        # -- End function
	.data
	.globl	g                               # @g
	.p2align	2, 0x0
g:
	.long	5                               # 0x5

	.section	.tls$,"dw"
	.globl	tl                              # @tl
	.p2align	2, 0x0
tl:
	.long	3                               # 0x3

	.section	.rdata,"dr"
.L.str:                                 # @.str
	.asciz	"hello"

	.data
	.globl	str                             # @str
	.p2align	3, 0x0
str:
	.quad	.L.str

	.bss
	.globl	bss_g                           # @bss_g
	.p2align	2, 0x0
bss_g:
	.long	0                               # 0x0

	.section	.rdata,"dr"
	.p2align	2, 0x0                          # @switch.table.sw
.Lswitch.table.sw:
	.ascii	"\003\007\t\013\r"

	.section	.rdata$.refptr.ext,"dr",discard,.refptr.ext
	.p2align	3, 0x0
	.globl	.refptr.ext
.refptr.ext:
	.quad	ext
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
