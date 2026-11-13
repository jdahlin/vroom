# x86-64 system, cache and port instructions with operands
	clflush (%rax)
	clflushopt 8(%rdi)
	clwb (%rsi,%rcx,8)
	fxsave (%rdi)
	fxrstorq (%rdi)
	xsave64 (%rdi)
	xrstors64 (%rdi)
	xsaves (%rbx)
	ldmxcsr -4(%rsp)
	lgdt (%rax)
	lidt 16(%rsp)
	sidt (%rax)
	invlpg (%rdi)
	lldt %ax
	ltr %ax
	str %eax
	sldt %eax
	verw (%rax)
	verw %ax
	cmpxchg16b (%rdi)
	lock cmpxchg16b (%rdi)
	rdrand %rax
	rdrand %eax
	rdseed %ecx
	rdpid %rax
	movnti %rax, (%rdi)
	movnti %ecx, 4(%rdi)
	movntiq %rax, (%rdi)
	lsl %ax, %eax
	lsl (%rax), %ecx
	lar %ax, %eax
	inb %dx, %al
	inw %dx, %ax
	inl %dx, %eax
	inb $0x80, %al
	outb %al, %dx
	outw %ax, %dx
	outl %eax, %dx
	outb %al, $0x80
	in %dx, %al
	out %al, $0x80
	int $0x80
	int $3
	vmxon (%rax)
	vmclear (%rax)
	vmptrld (%rax)
