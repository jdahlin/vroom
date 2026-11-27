# EVEX broadcasts of an xmm register, as llvm-mc
	.text
	vpbroadcastb	%xmm0, %zmm0
	vpbroadcastb	%xmm9, %zmm13
	vpbroadcastw	%xmm0, %zmm0
	vpbroadcastw	%xmm9, %zmm13
	vpbroadcastd	%xmm0, %zmm0
	vpbroadcastd	%xmm9, %zmm13
	vpbroadcastq	%xmm0, %zmm0
	vpbroadcastq	%xmm9, %zmm13
