# AVX2 permutes by a vector of indices, as llvm-mc
	.text
	vpermd	%ymm0, %ymm1, %ymm1
	vpermd	%ymm9, %ymm10, %ymm11
	vpermd	32(%rax,%rcx,4), %ymm2, %ymm3
	vpermps	%ymm0, %ymm1, %ymm1
	vpermps	%ymm12, %ymm3, %ymm13
	vpermps	(%r12), %ymm2, %ymm3
