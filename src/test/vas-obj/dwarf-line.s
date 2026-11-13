// .file N / .loc: the DWARF v5 line table as MC writes it
	.text
	.file 0 "/d" "m.c" md5 0x00112233445566778899aabbccddeeff
	.file 1 "/e" "h.h" md5 0x00112233445566778899aabbccddeef0
f:
	.loc 0 5 3
	nop
	.loc 1 200 1 discriminator 3
	nop
	.loc 1 2 9 isa 1 basic_block
	nop
	.loc 0 3 0
	.loc 0 4 1
	.space 400
	nop
	.loc 0 4 1
	.word 1
	.section .text.b,"ax",@progbits
	.loc 0 1000 4
	ret
	.p2align 9
	.text
	.loc 0 7
	ret
	.loc 0 9 3
	.section .data
	.byte 1
	.text
	nop
