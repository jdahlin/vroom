	.def	@feat.00;
	.scl	3;
	.type	0;
	.endef
	.globl	@feat.00
@feat.00 = 0
	.file	"misc.c"
	.text
	.comm	cm, 8, 3
	.comm	cm1, 4
	.comm	cm2, 16, 0
	.lcomm	lc, 8, 16
	.lcomm	lc2, 3
	.weak	wundef
	.def	.Lstat;
	.scl	3;
	.endef
.Lstat:
	ret
	.set	al, f2
	.globl	al
f2:
	ret
	.addrsig
	.addrsig_sym f2
	.addrsig_sym wundef
	.addrsig_sym .Lstat
	.addrsig_sym ext2
	.data
	.quad	wundef
	.quad	ext2
	.long	al
