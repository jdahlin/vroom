# the kernel's ALTERNATIVE padding: .skip with a count the layout settles (jumps relaxed)
	.text
771:	jmp 9f
	nop
772:
	.skip -(((775f-774f)-(772b-771b)) > 0) * ((775f-774f)-(772b-771b)),0x90
773:
	.pushsection .altinstr_replacement,"ax"
774:	movq %rax, %rbx
	movq %rax, %rbx
	movq %rax, %rbx
775:
	.popsection
	.byte (773b - 771b)
9:	ret
