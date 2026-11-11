1:	loop 1b
	loope 1b
	loopne 1b
	jrcxz 1b
	jecxz 1b
	ljmpl *(%rax)
	ljmpq *8(%rbx)
	lcalll *(%rcx)
	lcallq *(%r9)
	andn %eax,%ebx,%ecx
	andnq (%rax),%r9,%r10
	pdep %rax,%rbx,%rcx
	pext %r8d,%r9d,%r10d
	mulx %rax,%rbx,%rcx
	bextr %eax,%ebx,%ecx
	bzhiq %r8,(%rax),%r9
	sarx %eax,%ebx,%ecx
	shlx %rax,%rbx,%rcx
	shrx %r8d,%r9d,%r10d
	blsr %eax,%ebx
	blsmskq (%rax),%r11
	blsi %r12,%r13
	jmp 2f
2:
