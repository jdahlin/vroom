# BMI2 shifts, as llvm-mc
	.text
	shlxl	%ecx, %eax, %edx
	shlxq	%r9, %r10, %r11
	shlxq	%rax, 8(%r12,%r13,4), %rbx
	shlxl	%r8d, (%rdi), %r15d
	shrxl	%ecx, %eax, %edx
	shrxq	%r9, %r10, %r11
	shrxq	%rax, 8(%r12,%r13,4), %rbx
	shrxl	%r8d, (%rdi), %r15d
	sarxl	%ecx, %eax, %edx
	sarxq	%r9, %r10, %r11
	sarxq	%rax, 8(%r12,%r13,4), %rbx
	sarxl	%r8d, (%rdi), %r15d
