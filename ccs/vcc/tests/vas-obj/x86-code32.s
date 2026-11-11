	.text
	.code32
startup_32:
	cli
	call 1f
1:	popl %ebp
	leal 8(%esi), %esp
	movl %cr0, %eax
	btrl $31, %eax
	movl %eax, %cr0
	incl %ecx
	decw %ax
	pushl $0x10
	pushw %cx
	pushl %eax
	sgdtl (%eax)
	lgdtl 4(%ebx)
	movl abs_target, %eax
	movw abs2, %ax
	movb %al, abs3+4
	movl %eax, 0x1000
	ljmpl $0x10, $2f
2:	jnz other
	call other
	lret
	.code64
startup_64:
	pushq %rax
	pushw %ax
	call other
	ret
	.section .data..x,"a"
	.long 1
	.section .text.y,"a"
	ret
	.section .tdata.z,"a"
	.long 2
	.bss
	.fill 64, 1, 0
