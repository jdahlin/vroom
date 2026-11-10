// MC: a .file N whose name is the root file's is the root, N stays unassigned
// (file 2 here, the last number: no gap); "x/a.c" is another file
	.text
	.file	0 "/d" "a.c"
	.file	1 "x/a.c"
	.file	2 "/other" "a.c"
f:
	.loc	0 1 1
	nop
	.loc	1 2 1 prologue_end
	ret
