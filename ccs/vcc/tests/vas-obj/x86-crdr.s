# x86-64: mov to and from control and debug registers
	mov %r10,%cr3
	mov %cr3,%r10
	mov %cr4,%rax
	movq %rax, %cr0
	mov %r10, %db0
	mov %db7, %r10
	mov %rax, %cr8
	mov %dr6, %rax
	mov %cr2, %r9
