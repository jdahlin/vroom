/* bare-metal start (from ../emit/start.S, plus exit status and setjmp) for qemu -M virt: identity map (device below 1 GB, normal RAM above), stack, main(), semihosting exit */
	.section .text.start, "ax"
	.globl _start
_start:
	ldr	x0, =stack_top
	mov	sp, x0
	mov	x0, #(3 << 20)
	msr	cpacr_el1, x0
	ldr	x0, =0xff00
	msr	mair_el1, x0
	ldr	x0, =(25 | (1 << 8) | (1 << 10) | (3 << 12) | (1 << 23) | (2 << 32))
	msr	tcr_el1, x0
	adrp	x0, ttb
	msr	ttbr0_el1, x0
	isb
	mrs	x0, sctlr_el1
	orr	x0, x0, #1
	orr	x0, x0, #(1 << 2)
	orr	x0, x0, #(1 << 12)
	bic	x0, x0, #(1 << 1)
	msr	sctlr_el1, x0
	isb
	/* static TLS (variant 1): tpidr_el0 -> 16-byte TCB, .tdata after it, .tbss zero (in .bss) */
	adrp	x0, tls_area
	add	x0, x0, :lo12:tls_area
	msr	tpidr_el0, x0
	add	x1, x0, #16
	ldr	x2, =__tdata_start
	ldr	x3, =__tdata_size
2:	cbz	x3, 3f
	ldrb	w4, [x2], #1
	strb	w4, [x1], #1
	sub	x3, x3, #1
	b	2b
3:	bl	__rt_main
	/* x0 = exit status */
	.globl	_exit
_exit:
	/* semihosting SYS_EXIT with {ADP_Stopped_ApplicationExit, code}: qemu exits with code */
	adrp	x1, exitblk
	add	x1, x1, :lo12:exitblk
	str	x0, [x1, #8]
	mov	x0, #0x18
	hlt	#0xf000
1:	b	1b

	.globl	setjmp
	.globl	_setjmp
setjmp:
_setjmp:
	stp	x19, x20, [x0, #0]
	stp	x21, x22, [x0, #16]
	stp	x23, x24, [x0, #32]
	stp	x25, x26, [x0, #48]
	stp	x27, x28, [x0, #64]
	stp	x29, x30, [x0, #80]
	mov	x2, sp
	str	x2, [x0, #96]
	stp	d8, d9, [x0, #104]
	stp	d10, d11, [x0, #120]
	stp	d12, d13, [x0, #136]
	stp	d14, d15, [x0, #152]
	mov	w0, #0
	ret

	.globl	longjmp
	.globl	_longjmp
longjmp:
_longjmp:
	ldp	x19, x20, [x0, #0]
	ldp	x21, x22, [x0, #16]
	ldp	x23, x24, [x0, #32]
	ldp	x25, x26, [x0, #48]
	ldp	x27, x28, [x0, #64]
	ldp	x29, x30, [x0, #80]
	ldr	x2, [x0, #96]
	mov	sp, x2
	ldp	d8, d9, [x0, #104]
	ldp	d10, d11, [x0, #120]
	ldp	d12, d13, [x0, #136]
	ldp	d14, d15, [x0, #152]
	cmp	w1, #0
	csinc	w0, w1, wzr, ne
	ret

	.data
	.p2align 3
exitblk:
	.quad	0x20026, 0
	.section .rodata.ttb, "a"
	.p2align 12
ttb:
	.quad	0x00000000 | 0x401
	.quad	0x40000000 | 0x705
	.quad	0x80000000 | 0x705
	.quad	0xc0000000 | 0x705
	.fill	508, 8, 0

	.bss
	.p2align 6
tls_area:
	.space	64 << 10
	.p2align 4
	.space	8 << 20
stack_top:
