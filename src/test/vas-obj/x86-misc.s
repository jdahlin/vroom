# x86: alias of an alias (X_ = X0), segment prefix words, in/out (%dx), . = expr
X0 = %xmm4
X1 = %xmm5
X_ = X0
X0 = X1
X1 = X_
	pxor X0, X1
	ds clflush (%r10)
	ds
	nop
	inl (%dx), %eax
	outb %al, (%dx)
v:	.byte 1
	. = v + 8
	nop
