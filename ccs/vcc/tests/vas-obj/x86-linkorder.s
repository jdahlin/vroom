# x86: SHF_LINK_ORDER sections, one per linked symbol (__patchable_function_entries)
	.text
f:	nop
	.pushsection __patchable_function_entries,"awo",@progbits,f
	.p2align 3
	.quad f
	.popsection
	.section .init.text,"ax",@progbits
g:	nop
	.pushsection __patchable_function_entries,"awo",@progbits,g
	.p2align 3
	.quad g
	.popsection
	.text
h:	ret
	.pushsection __patchable_function_entries,"awo",@progbits,h
	.p2align 3
	.quad h
	.popsection
