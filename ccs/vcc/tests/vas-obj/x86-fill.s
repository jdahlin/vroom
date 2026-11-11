# x86: .fill with a count the layout settles (the IDT entry stubs: .fill 0b + N - ., 1, 0xcc)
	.text
h:
	.rept 3
0:	pushq $0
	jmp 9f
	.fill 0b + 9 - ., 1, 0xcc
	.endr
	.fill 64 - (. - h), 1, 0xcc
9:	ret
