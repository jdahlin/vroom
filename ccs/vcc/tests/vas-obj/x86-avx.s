# x86: AVX/AVX2 (VEX), BMI2 rorx: the kernel's crypto code
	vpaddd %ymm1, %ymm2, %ymm3
	vpaddd %xmm1, %xmm2, %xmm3
	vpxor %ymm8, %ymm9, %ymm10
	vpxor (%rax), %ymm1, %ymm2
	vpshufb %ymm10, %ymm0, %ymm11
	vpaddq 32(%rsi,%rcx,8), %ymm4, %ymm4
	vmovdqa %ymm0, %ymm1
	vmovdqa (%rdi), %ymm12
	vmovdqa %ymm12, (%rdi)
	vmovdqu %xmm9, %xmm1
	vmovdqu 16(%rsi), %xmm3
	vpshufd $0x1b, %ymm1, %ymm2
	vpsrld $7, %ymm1, %ymm2
	vpslldq $8, %xmm9, %xmm10
	vpsrlq $3, %ymm13, %ymm14
	vpclmulqdq $0x11, %xmm1, %xmm2, %xmm3
	vpalignr $4, %ymm1, %ymm2, %ymm3
	vperm2i128 $0x20, %ymm1, %ymm2, %ymm3
	vinserti128 $1, %xmm4, %ymm5, %ymm6
	vinserti128 $1, (%rax), %ymm5, %ymm6
	vextracti128 $1, %ymm3, %xmm4
	vpbroadcastd %xmm0, %ymm1
	vbroadcasti128 (%rax), %ymm2
	vpermq $0x4e, %ymm1, %ymm2
	vpunpcklqdq %ymm1, %ymm2, %ymm3
	vmovd %eax, %xmm1
	vmovd %xmm1, %eax
	vmovq %rax, %xmm1
	vmovq %xmm1, %rax
	vmovq (%rax), %xmm2
	vmovq %xmm2, (%rax)
	vaesenc %ymm1, %ymm2, %ymm3
	vaesenclast %xmm1, %xmm2, %xmm3
	vpmuludq %ymm1, %ymm2, %ymm3
	vpinsrq $1, %rax, %xmm1, %xmm2
	vpextrq $1, %xmm1, %rax
	vpextrd $2, %xmm1, %ecx
	vpblendvb %ymm4, %ymm3, %ymm2, %ymm1
	vpblendd $0xf0, %ymm1, %ymm2, %ymm3
	vpsrlvq %ymm1, %ymm2, %ymm3
	vpmovzxbd (%rax), %ymm1
	rorx $13, %eax, %ebx
	rorx $3, %r9, %r10
	vperm2f128 $1, %ymm1, %ymm2, %ymm3
	vinsertf128 $1, %xmm1, %ymm2, %ymm3
	vpand %ymm1, %ymm2, %ymm3
	vpor %ymm1, %ymm2, %ymm3
	vpsrad $3, %ymm1, %ymm1
