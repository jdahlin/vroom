# .file 0 selects DWARF 5 for MinGW as in MC: .debug_line_str (written after .pdata and
# .llvm_addrsig) referenced through IMAGE_REL_AMD64_SECREL
	.text
	.file	0 "/src/dir" "m.c" md5 0x00112233445566778899aabbccddeeff
	.file	1 "/src/dir" "h.h" md5 0x00112233445566778899aabbccddeef0
	.def	f;
	.scl	2;
	.type	32;
	.endef
	.globl	f
f:
.seh_proc f
	pushq	%rbp
	.seh_pushreg %rbp
	.seh_endprologue
	.loc	0 5 3 prologue_end
	nop
	.loc	1 2 1
	popq	%rbp
	retq
	.seh_endproc
	.addrsig
	.addrsig_sym f
