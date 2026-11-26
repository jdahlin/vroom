	.text
	rcpss	%xmm1, %xmm2
	rcpss	%xmm9, %xmm14
	rcpss	8(%rax), %xmm3
	rsqrtss	%xmm1, %xmm2
	rsqrtss	%xmm9, %xmm14
	rsqrtss	8(%rax), %xmm3
	addsubps	%xmm1, %xmm2
	addsubps	%xmm9, %xmm14
	addsubps	8(%rax), %xmm3
	addsubpd	%xmm1, %xmm2
	addsubpd	%xmm9, %xmm14
	addsubpd	8(%rax), %xmm3
	haddps	%xmm1, %xmm2
	haddps	%xmm9, %xmm14
	haddps	8(%rax), %xmm3
	haddpd	%xmm1, %xmm2
	haddpd	%xmm9, %xmm14
	haddpd	8(%rax), %xmm3
	hsubps	%xmm1, %xmm2
	hsubps	%xmm9, %xmm14
	hsubps	8(%rax), %xmm3
	hsubpd	%xmm1, %xmm2
	hsubpd	%xmm9, %xmm14
	hsubpd	8(%rax), %xmm3
	phaddsw	%xmm1, %xmm2
	phaddsw	%xmm9, %xmm14
	phaddsw	8(%rax), %xmm3
	phsubw	%xmm1, %xmm2
	phsubw	%xmm9, %xmm14
	phsubw	8(%rax), %xmm3
	phsubd	%xmm1, %xmm2
	phsubd	%xmm9, %xmm14
	phsubd	8(%rax), %xmm3
	phsubsw	%xmm1, %xmm2
	phsubsw	%xmm9, %xmm14
	phsubsw	8(%rax), %xmm3
	phminposuw	%xmm1, %xmm2
	phminposuw	%xmm9, %xmm14
	phminposuw	8(%rax), %xmm3
	lddqu	(%rax), %xmm1
	lddqu	16(%r9), %xmm12
	blendvps	%xmm0, %xmm1, %xmm2
	blendvps	%xmm0, %xmm9, %xmm14
	blendvpd	%xmm0, %xmm1, %xmm2
	blendvpd	%xmm0, %xmm9, %xmm14
	roundps	$1, %xmm1, %xmm2
	roundps	$3, %xmm9, %xmm14
	roundps	$2, (%rax), %xmm3
	roundpd	$1, %xmm1, %xmm2
	roundpd	$3, %xmm9, %xmm14
	roundpd	$2, (%rax), %xmm3
	blendpd	$1, %xmm1, %xmm2
	blendpd	$3, %xmm9, %xmm14
	blendpd	$2, (%rax), %xmm3
	insertps	$1, %xmm1, %xmm2
	insertps	$3, %xmm9, %xmm14
	insertps	$2, (%rax), %xmm3
	dpps	$1, %xmm1, %xmm2
	dpps	$3, %xmm9, %xmm14
	dpps	$2, (%rax), %xmm3
	dppd	$1, %xmm1, %xmm2
	dppd	$3, %xmm9, %xmm14
	dppd	$2, (%rax), %xmm3
	mpsadbw	$1, %xmm1, %xmm2
	mpsadbw	$3, %xmm9, %xmm14
	mpsadbw	$2, (%rax), %xmm3
