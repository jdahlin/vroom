	vaesenclast %zmm1, %zmm2, %zmm3
	vaesenc %zmm17, %zmm2, %zmm30
	vaesenc %ymm1, %ymm2, %ymm3
	vpaddq %zmm1, %zmm2, %zmm3
	vpxord 64(%rsi), %zmm2, %zmm3
	vpxord 0x1000(%rsi), %zmm2, %zmm3
	vpxord 8(%rsi), %zmm2, %zmm3
	vpxord (%rsi), %zmm2, %zmm3
	vmovdqu8 %zmm1, 128(%rdi)
	vmovdqu8 (%rsi), %zmm1 {%k1} {z}
	vmovdqu8 %zmm1, (%rdi) {%k2}
	vmovdqa64 %zmm1, %zmm2
	vmovdqa64 %zmm17, %zmm25
	vpclmulqdq $0x11, %zmm1, %zmm2, %zmm3
	vpternlogd $0x96, %zmm1, %zmm2, %zmm3
	vpternlogq $0x96, 64(%rsi), %zmm2, %zmm3
	vpternlogq $0x96, (%rsi), %ymm2, %ymm3
	vpternlogq $0x96, %xmm1, %xmm2, %xmm3
	vpshufb %zmm1, %zmm2, %zmm3
	vprord $7, %xmm1, %xmm2
	vprord $12, %xmm21, %xmm2
	vbroadcasti32x4 (%rax), %zmm1
	vbroadcasti32x4 -16(%rcx), %zmm1
	vbroadcasti32x4 32(%r9), %zmm1
	vpsllq $1, %zmm1, %zmm2
	vpsrldq $8, %zmm1, %zmm2
	vpermi2d %ymm1, %ymm2, %ymm3
	vpbroadcastq %rcx, %zmm1
	vextracti64x4 $1, %zmm1, %ymm2
	vextracti64x4 $1, %zmm1, 32(%rax)
	vpxorq (%rsi), %zmm1, %zmm2
	vpshufb %ymm1, %ymm2, %ymm3
	vpaddq %ymm17, %ymm2, %ymm3
