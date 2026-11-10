	.file	"a.c"
	.text
	.globl	f                               // -- Begin function f
	.p2align	2
	.type	f,@function
f:                                      // @f
	.cfi_startproc
// %bb.0:
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	.cfi_def_cfa_offset 16
	mov	x29, sp
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	bl	g
	add	w0, w0, #1
	.cfi_def_cfa wsp, 16
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	ret
.Lfunc_end0:
	.size	f, .Lfunc_end0-f
	.cfi_endproc
                                        // -- End function
	.globl	h                               // -- Begin function h
	.p2align	2
	.type	h,@function
h:                                      // @h
	.cfi_startproc
// %bb.0:
	mov	w0, #3                          // =0x3
	ret
.Lfunc_end1:
	.size	h, .Lfunc_end1-h
	.cfi_endproc
                                        // -- End function
	.ident	"Homebrew clang version 23.1.4"
	.section	".note.GNU-stack","",@progbits
	.addrsig
