	vpxor %xmm15, %xmm4, %xmm3
	vpxor %xmm15, %xmm14, %xmm3
	vpaddd %ymm9, %ymm1, %ymm2
	vpsubd %xmm9, %xmm1, %xmm2
	kmovq %rax, %k1
	kmovd %eax, %k2
	kmovw %eax, %k3
	kmovb %eax, %k4
	kmovq %k1, %rax
	kmovd %k2, %r9d
