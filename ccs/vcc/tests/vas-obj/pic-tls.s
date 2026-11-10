// clang -O2 -fPIC -S: GOT, TLS descriptors; .type @object in .tbss stays STT_TLS
	.text
	.globl	f                               // -- Begin function f
	.p2align	2
	.type	f,@function
f:                                      // @f
	.cfi_startproc
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	.cfi_def_cfa_offset 32
	str	x19, [sp, #16]                  // 8-byte Spill
	mov	x29, sp
	.cfi_def_cfa w29, 32
	.cfi_offset w19, -16
	.cfi_offset w30, -24
	.cfi_offset w29, -32
	adrp	x8, :got:g
	ldr	x8, [x8, :got_lo12:g]
	ldr	w19, [x8]
	bl	h
	add	w0, w0, w19
	.cfi_def_cfa wsp, 32
	ldr	x19, [sp, #16]                  // 8-byte Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	.cfi_def_cfa_offset 0
	.cfi_restore w19
	.cfi_restore w30
	.cfi_restore w29
	ret
.Lfunc_end0:
	.size	f, .Lfunc_end0-f
	.cfi_endproc
	.globl	tp                              // -- Begin function tp
	.p2align	2
	.type	tp,@function
tp:                                     // @tp
	.cfi_startproc
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	.cfi_def_cfa_offset 16
	mov	x29, sp
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	adrp	x0, :tlsdesc:t
	ldr	x1, [x0, :tlsdesc_lo12:t]
	add	x0, x0, :tlsdesc_lo12:t
	.tlsdesccall t
	blr	x1
	mrs	x8, TPIDR_EL0
	add	x0, x8, x0
	.cfi_def_cfa wsp, 16
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	ret
.Lfunc_end1:
	.size	tp, .Lfunc_end1-tp
	.cfi_endproc
	.type	t,@object                       // @t
	.section	.tbss,"awT",@nobits
	.globl	t
	.p2align	2, 0x0
t:
	.word	0                               // 0x0
	.size	t, 4

	.section	".note.GNU-stack","",@progbits
	.addrsig
