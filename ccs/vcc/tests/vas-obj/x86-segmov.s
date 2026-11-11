# x86: mov to and from segment registers (head_64.S: movl %eax, %ds was encoded as a move to %esi)
	movl %eax, %ds
	movw %ax, %ss
	movq %rax, %es
	mov %eax, %fs
	mov %ax, %gs
	movl %ds, %eax
	movw %ss, %ax
	mov %cs, %rax
	movw %ds, (%rdi)
	mov %eax, %ds
	movl %r8d, %ds
