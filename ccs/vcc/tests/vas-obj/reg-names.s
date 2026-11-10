	.text
	.globl	f
f:
	bl	p2
	b	z1
	bl	pn1
	tbz	x0, #1, p4
	adrp	x0, d1
	add	x0, x0, :lo12:d1
	adr	x1, v3
	ret
