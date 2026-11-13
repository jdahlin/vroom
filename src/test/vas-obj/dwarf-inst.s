// Raw instructions consume a pending .loc before their first byte, as mnemonics do.
	.text
	.file 0 "/d" "raw.c"
f:
	.loc 0 10 2 prologue_end
	.inst 0xd5033bbf
	.loc 0 11 4
	.inst 0xd503201f, 0xd5033bbf
	.loc 0 12 6
	nop
	.loc 0 13 8
	.inst 0xd503201f
	.loc 0 14 10
	ret
