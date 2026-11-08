/* bare-metal start for qemu -M virt: identity map (device below 1 GB, normal RAM above), stack, main(), semihosting exit */
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
	bl	main
	mov	x1, #0x26
	movk	x1, #2, lsl #16
	mov	x0, #0x18
	hlt	#0xf000
1:	b	1b

	.section .rodata.ttb, "a"
	.p2align 12
ttb:
	.quad	0x00000000 | 0x401
	.quad	0x40000000 | 0x705
	.quad	0x80000000 | 0x705
	.quad	0xc0000000 | 0x705
	.fill	508, 8, 0

	.bss
	.p2align 4
	.space	1 << 20
stack_top:
