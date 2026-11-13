# x86: integer suffixes U, L, UL, ULL in expressions (C headers' constants in .S), as MC
	andl $0x0700a169U,%edx
	movl $12UL, %eax
	movq $5ull, %rax
	andl $(0x0700a169U & ((1<<(( 0*32+25) & 31)))),%edx
