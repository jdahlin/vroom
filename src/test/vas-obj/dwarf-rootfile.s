// .file N naming the root file is the root (MC): .loc 1 then refers to an unassigned
// number in MC; here file 1 differs only in its directory part, so .loc 2 is used
	.text
	.file	0 "/d" "a.c"
	.file	1 "/other" "a.c"
	.file	2 "x/a.c"
f:
	.loc	0 1 1
	nop
	.loc	2 2 1 prologue_end
	ret
