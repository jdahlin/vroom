# .file N / .loc for MinGW: DWARF 4 line tables (clang's default for the target), no .debug_line
# section declared: include directories and file names in .debug_line itself, MC's "dr" section
	.text
	.file	1 "/src/dir" "m.c"
	.file	2 "/src/dir" "sub/h.h"
	.file	3 "/usr/include/stdio.h"
	.file	4 "x.h"
	.file	5 "/src/dir" "n.c"
	.def	f;
	.scl	2;
	.type	32;
	.endef
	.globl	f
f:
	.loc	1 5 3 prologue_end
	nop
	.loc	2 200 1 is_stmt 0
	nop
	.loc	3 2 9 discriminator 3
	nop
	.loc	4 3 0 is_stmt 1
	.space	400
	nop
	.loc	5 7 1 epilogue_begin
	retq
	.section	.text$g,"xr",one_only,g
	.globl	g
g:
	.loc	1 1000 4
	retq
	.section	.debug_abbrev,"dr"
	.byte	0
