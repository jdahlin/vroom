# x86: CET shadow stack, invpcid, ret/lret with an immediate
	wrussq %rax,(%rbx)
	wrssq %rax,(%rbx)
	wrussd %eax,(%rbx)
	incsspq %rax
	rdsspq %rax
	invpcid (%rax), %rcx
	ret $8
	lret $16
	lretq
