# x86: SSSE3/SSE4.1, AES-NI, PCLMUL, SHA, CRC32, FS/GS base, x87 state
	pshufb %xmm1, %xmm2
	pshufb (%rax), %xmm9
	palignr $8, %xmm3, %xmm4
	pclmulqdq $0x11, %xmm5, %xmm6
	pclmulqdq $0, 16(%rdi), %xmm12
	pmovzxdq %xmm0, %xmm1
	movntdqa (%rsi), %xmm2
	aesenc %xmm1, %xmm0
	aesenclast %xmm8, %xmm0
	aesdec (%rax), %xmm1
	aesdeclast %xmm1, %xmm2
	aesimc %xmm3, %xmm4
	aeskeygenassist $1, %xmm0, %xmm1
	sha1rnds4 $3, %xmm1, %xmm2
	sha1nexte %xmm1, %xmm2
	sha1msg1 %xmm3, %xmm4
	sha1msg2 %xmm3, %xmm4
	sha256rnds2 %xmm0, %xmm1, %xmm2
	sha256msg1 %xmm10, %xmm11
	sha256msg2 (%rax), %xmm11
	crc32b %al, %eax
	crc32b %sil, %eax
	crc32w %ax, %eax
	crc32l %ecx, %eax
	crc32q %rcx, %rax
	crc32q (%rsi), %r8
	crc32b (%rsi), %eax
	pinsrd $1, %eax, %xmm0
	pinsrq $1, %rax, %xmm0
	pextrd $2, %xmm3, %ecx
	pextrq $1, %xmm3, %rcx
	rdfsbase %rax
	wrgsbase %rbx
	rdgsbase %ecx
	emms
	fnclex
	fnsave (%rax)
	frstor (%rax)
	mwaitx
	monitorx
	pmulld %xmm1, %xmm2
	ptest %xmm1, %xmm2
