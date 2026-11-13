	.def	fn;
	.scl	2;
	.type	32;
	.endef
	.def	sfn;
	.scl	3;
	.type	32;
	.endef
	.text
	.globl	fn
fn:
	call	sfn
	call	glab
	call	.Lloc
	call	ext
	jmp	sfn
	jmp	glab
	jmp	ext
	jmp	wk
	movl	$5, g(%rip)
	movb	$5, g(%rip)
	movw	$5, g(%rip)
	movq	$5, g(%rip)
	leaq	g+8(%rip), %rax
	movl	tl@SECREL32(%rcx), %eax
	movl	$tl@SECREL32, %eax
	movl	$g@IMGREL, %eax
	movabsq	$g, %rax
	movl	$g, %eax
	movq	$g, %rax
	ret
sfn:
	ret
	.globl	glab
glab:
	ret
.Lloc:
	ret
	.weak	wk
wk:
	ret
	.data
g:	.long	1
	.long	g
	.quad	g
	.long	g@IMGREL
	.long	g@SECREL32
	.quad	g@IMGREL
	.long	ext@IMGREL+4
	.secrel32	g
	.secrel32	g+12
	.secidx	g
	.secidx	ext
	.rva	g, ext+4, fn-8
	.symidx	g
	.symidx	ext
	.long	.Lloc
	.quad	.Lloc
	.long	.Lloc@IMGREL
	.long	.Lend-g
	.long	glab-fn
	.quad	glab-fn
	.byte	g-.Lend
.Lend:
	.section .tls$,"dw"
tl:	.long 3
	.section .rdata,"dr"
	.long	g-.
	.quad	g-.
