// .prefalign: min(2^p2, the power of two holding the function); alignment
// after one is relaxed at the end (its frag's start is not known yet)
	.text
	nop
	.prefalign 5, .Le1, nop
f1:
	.cfi_startproc
	nop
	nop
	nop
	.p2align 4
	nop
	.cfi_endproc
.Le1:
	.prefalign 4, .Le2, nop
f2:
	ret
.Le2:
	.p2align 5, , 8
	nop
	.prefalign 6, .Le3, nop
f3:
	.fill 30, 4, 0xd503201f
.Le3:
	.size f1, .Le1-f1
	.section .rodata
	.byte 1
	.prefalign 3, .Le4, 0
	.byte 1, 2, 3
.Le4:
