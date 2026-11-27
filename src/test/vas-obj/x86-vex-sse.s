# VEX forms the assembler derives from the SSE encodings, as llvm-mc
	.text
	vmovd	%eax, %xmm1
	vmovd	%xmm9, %r8d
	vmovd	(%rax), %xmm2
	vmovd	%xmm2, 4(%rsp)
	vmovq	%rax, %xmm1
	vmovq	%xmm9, %r8
	vmovq	(%rax), %xmm2
	vmovq	%xmm2, 4(%rsp)
	vmovq	%xmm1, %xmm2
	vmovaps	(%rax), %xmm2
	vmovaps	%xmm9, 16(%rsp)
	vmovaps	%xmm1, %xmm12
	vmovups	(%rax), %xmm2
	vmovups	%xmm9, 16(%rsp)
	vmovups	%xmm1, %xmm12
	vmovapd	(%rax), %xmm2
	vmovapd	%xmm9, 16(%rsp)
	vmovapd	%xmm1, %xmm12
	vmovupd	(%rax), %xmm2
	vmovupd	%xmm9, 16(%rsp)
	vmovupd	%xmm1, %xmm12
	vmovdqa	(%rax), %xmm2
	vmovdqa	%xmm9, 16(%rsp)
	vmovdqa	%xmm1, %xmm12
	vmovdqu	(%rax), %xmm2
	vmovdqu	%xmm9, 16(%rsp)
	vmovdqu	%xmm1, %xmm12
	vlddqu	(%rax), %xmm2
	vmovntdq	%xmm2, (%rax)
	vmovmskps	%xmm9, %eax
	vmovmskpd	%xmm9, %eax
	vpmovmskb	%xmm9, %eax
	vpshufd	$1, %xmm1, %xmm2
	vpshufd	$1, (%rax), %xmm9
	vpshufhw	$1, %xmm1, %xmm2
	vpshufhw	$1, (%rax), %xmm9
	vpshuflw	$1, %xmm1, %xmm2
	vpshuflw	$1, (%rax), %xmm9
	vptest	%xmm1, %xmm2
	vptest	(%rax), %xmm9
	vpabsb	%xmm1, %xmm2
	vpabsb	(%rax), %xmm9
	vpabsw	%xmm1, %xmm2
	vpabsw	(%rax), %xmm9
	vpabsd	%xmm1, %xmm2
	vpabsd	(%rax), %xmm9
	vphminposuw	%xmm1, %xmm2
	vphminposuw	(%rax), %xmm9
	vpmovsxbw	%xmm1, %xmm2
	vpmovsxbw	(%rax), %xmm9
	vpmovsxdq	%xmm1, %xmm2
	vpmovsxdq	(%rax), %xmm9
	vpmovzxbw	%xmm1, %xmm2
	vpmovzxbw	(%rax), %xmm9
	vpmovzxbd	%xmm1, %xmm2
	vpmovzxbd	(%rax), %xmm9
	vpmovzxbq	%xmm1, %xmm2
	vpmovzxbq	(%rax), %xmm9
	vpmovzxwd	%xmm1, %xmm2
	vpmovzxwd	(%rax), %xmm9
	vpmovzxwq	%xmm1, %xmm2
	vpmovzxwq	(%rax), %xmm9
	vpmovzxdq	%xmm1, %xmm2
	vpmovzxdq	(%rax), %xmm9
	vsqrtps	%xmm1, %xmm2
	vsqrtps	(%rax), %xmm9
	vsqrtpd	%xmm1, %xmm2
	vsqrtpd	(%rax), %xmm9
	vrcpps	%xmm1, %xmm2
	vrcpps	(%rax), %xmm9
	vrsqrtps	%xmm1, %xmm2
	vrsqrtps	(%rax), %xmm9
	vroundps	$1, %xmm1, %xmm2
	vroundps	$1, (%rax), %xmm9
	vroundpd	$1, %xmm1, %xmm2
	vroundpd	$1, (%rax), %xmm9
	vucomiss	%xmm1, %xmm2
	vucomiss	(%rax), %xmm9
	vucomisd	%xmm1, %xmm2
	vucomisd	(%rax), %xmm9
	vcomiss	%xmm1, %xmm2
	vcomiss	(%rax), %xmm9
	vcomisd	%xmm1, %xmm2
	vcomisd	(%rax), %xmm9
	vcvtss2si	%xmm1, %eax
	vcvtss2si	(%rax), %eax
	vcvtsd2si	%xmm1, %eax
	vcvtsd2si	(%rax), %eax
	vcvttss2si	%xmm1, %eax
	vcvttss2si	(%rax), %eax
	vcvttsd2si	%xmm1, %eax
	vcvttsd2si	(%rax), %eax
	vcvtss2siq	%xmm1, %rax
	vcvtss2siq	(%rax), %rax
	vcvtsd2siq	%xmm1, %rax
	vcvtsd2siq	(%rax), %rax
	vcvttss2siq	%xmm1, %rax
	vcvttss2siq	(%rax), %rax
	vcvttsd2siq	%xmm1, %rax
	vcvttsd2siq	(%rax), %rax
	vcvtss2sil	%xmm1, %eax
	vcvtss2sil	(%rax), %eax
	vcvtsd2sil	%xmm1, %eax
	vcvtsd2sil	(%rax), %eax
	vcvttss2sil	%xmm1, %eax
	vcvttss2sil	(%rax), %eax
	vcvttsd2sil	%xmm1, %eax
	vcvttsd2sil	(%rax), %eax
	vcvtdq2ps	%xmm1, %xmm2
	vcvtdq2ps	(%rax), %xmm9
	vcvtps2dq	%xmm1, %xmm2
	vcvtps2dq	(%rax), %xmm9
	vcvttps2dq	%xmm1, %xmm2
	vcvttps2dq	(%rax), %xmm9
	vcvtps2pd	%xmm1, %xmm2
	vcvtps2pd	(%rax), %xmm9
	vcvtpd2ps	%xmm1, %xmm2
	vcvtdq2pd	%xmm1, %xmm2
	vcvtdq2pd	(%rax), %xmm9
	vcvtpd2dq	%xmm1, %xmm2
	vcvttpd2dq	%xmm1, %xmm2
	vpextrb	$1, %xmm2, %eax
	vpextrw	$1, %xmm2, %eax
	vpextrd	$1, %xmm2, %eax
	vpextrq	$1, %xmm2, %rax
	vaesimc	%xmm1, %xmm2
	vaesimc	(%rax), %xmm9
	vaeskeygenassist	$1, %xmm1, %xmm2
	vaeskeygenassist	$1, (%rax), %xmm9
	vmovss	(%rax), %xmm2
	vmovss	%xmm2, 4(%rsp)
	vmovss	%xmm1, %xmm2, %xmm2
	vmovsd	(%rax), %xmm2
	vmovsd	%xmm2, 4(%rsp)
	vmovsd	%xmm1, %xmm2, %xmm2
	vmovlps	(%rax), %xmm2, %xmm2
	vmovlps	%xmm2, 4(%rsp)
	vmovhps	(%rax), %xmm2, %xmm2
	vmovhps	%xmm2, 4(%rsp)
	vmovlpd	(%rax), %xmm2, %xmm2
	vmovlpd	%xmm2, 4(%rsp)
	vmovhpd	(%rax), %xmm2, %xmm2
	vmovhpd	%xmm2, 4(%rsp)
	vpaddb	%xmm1, %xmm2, %xmm2
	vpaddb	16(%rax), %xmm9, %xmm9
	vpaddw	%xmm1, %xmm2, %xmm2
	vpaddw	16(%rax), %xmm9, %xmm9
	vpaddd	%xmm1, %xmm2, %xmm2
	vpaddd	16(%rax), %xmm9, %xmm9
	vpaddq	%xmm1, %xmm2, %xmm2
	vpaddq	16(%rax), %xmm9, %xmm9
	vpsubb	%xmm1, %xmm2, %xmm2
	vpsubb	16(%rax), %xmm9, %xmm9
	vpsubw	%xmm1, %xmm2, %xmm2
	vpsubw	16(%rax), %xmm9, %xmm9
	vpsubd	%xmm1, %xmm2, %xmm2
	vpsubd	16(%rax), %xmm9, %xmm9
	vpsubq	%xmm1, %xmm2, %xmm2
	vpsubq	16(%rax), %xmm9, %xmm9
	vpaddsb	%xmm1, %xmm2, %xmm2
	vpaddsb	16(%rax), %xmm9, %xmm9
	vpaddsw	%xmm1, %xmm2, %xmm2
	vpaddsw	16(%rax), %xmm9, %xmm9
	vpaddusb	%xmm1, %xmm2, %xmm2
	vpaddusb	16(%rax), %xmm9, %xmm9
	vpaddusw	%xmm1, %xmm2, %xmm2
	vpaddusw	16(%rax), %xmm9, %xmm9
	vpsubsb	%xmm1, %xmm2, %xmm2
	vpsubsb	16(%rax), %xmm9, %xmm9
	vpsubsw	%xmm1, %xmm2, %xmm2
	vpsubsw	16(%rax), %xmm9, %xmm9
	vpsubusb	%xmm1, %xmm2, %xmm2
	vpsubusb	16(%rax), %xmm9, %xmm9
	vpsubusw	%xmm1, %xmm2, %xmm2
	vpsubusw	16(%rax), %xmm9, %xmm9
	vpmullw	%xmm1, %xmm2, %xmm2
	vpmullw	16(%rax), %xmm9, %xmm9
	vpmulld	%xmm1, %xmm2, %xmm2
	vpmulld	16(%rax), %xmm9, %xmm9
	vpmulhw	%xmm1, %xmm2, %xmm2
	vpmulhw	16(%rax), %xmm9, %xmm9
	vpmulhuw	%xmm1, %xmm2, %xmm2
	vpmulhuw	16(%rax), %xmm9, %xmm9
	vpmulhrsw	%xmm1, %xmm2, %xmm2
	vpmulhrsw	16(%rax), %xmm9, %xmm9
	vpmuludq	%xmm1, %xmm2, %xmm2
	vpmuludq	16(%rax), %xmm9, %xmm9
	vpmuldq	%xmm1, %xmm2, %xmm2
	vpmuldq	16(%rax), %xmm9, %xmm9
	vpmaddwd	%xmm1, %xmm2, %xmm2
	vpmaddwd	16(%rax), %xmm9, %xmm9
	vpmaddubsw	%xmm1, %xmm2, %xmm2
	vpmaddubsw	16(%rax), %xmm9, %xmm9
	vpsadbw	%xmm1, %xmm2, %xmm2
	vpsadbw	16(%rax), %xmm9, %xmm9
	vpavgb	%xmm1, %xmm2, %xmm2
	vpavgb	16(%rax), %xmm9, %xmm9
	vpavgw	%xmm1, %xmm2, %xmm2
	vpavgw	16(%rax), %xmm9, %xmm9
	vpand	%xmm1, %xmm2, %xmm2
	vpand	16(%rax), %xmm9, %xmm9
	vpandn	%xmm1, %xmm2, %xmm2
	vpandn	16(%rax), %xmm9, %xmm9
	vpor	%xmm1, %xmm2, %xmm2
	vpor	16(%rax), %xmm9, %xmm9
	vpxor	%xmm1, %xmm2, %xmm2
	vpxor	16(%rax), %xmm9, %xmm9
	vpcmpeqb	%xmm1, %xmm2, %xmm2
	vpcmpeqb	16(%rax), %xmm9, %xmm9
	vpcmpeqw	%xmm1, %xmm2, %xmm2
	vpcmpeqw	16(%rax), %xmm9, %xmm9
	vpcmpeqd	%xmm1, %xmm2, %xmm2
	vpcmpeqd	16(%rax), %xmm9, %xmm9
	vpcmpeqq	%xmm1, %xmm2, %xmm2
	vpcmpeqq	16(%rax), %xmm9, %xmm9
	vpcmpgtb	%xmm1, %xmm2, %xmm2
	vpcmpgtb	16(%rax), %xmm9, %xmm9
	vpcmpgtw	%xmm1, %xmm2, %xmm2
	vpcmpgtw	16(%rax), %xmm9, %xmm9
	vpcmpgtd	%xmm1, %xmm2, %xmm2
	vpcmpgtd	16(%rax), %xmm9, %xmm9
	vpcmpgtq	%xmm1, %xmm2, %xmm2
	vpcmpgtq	16(%rax), %xmm9, %xmm9
	vpminsb	%xmm1, %xmm2, %xmm2
	vpminsb	16(%rax), %xmm9, %xmm9
	vpminsw	%xmm1, %xmm2, %xmm2
	vpminsw	16(%rax), %xmm9, %xmm9
	vpminsd	%xmm1, %xmm2, %xmm2
	vpminsd	16(%rax), %xmm9, %xmm9
	vpminub	%xmm1, %xmm2, %xmm2
	vpminub	16(%rax), %xmm9, %xmm9
	vpminuw	%xmm1, %xmm2, %xmm2
	vpminuw	16(%rax), %xmm9, %xmm9
	vpminud	%xmm1, %xmm2, %xmm2
	vpminud	16(%rax), %xmm9, %xmm9
	vpmaxsb	%xmm1, %xmm2, %xmm2
	vpmaxsb	16(%rax), %xmm9, %xmm9
	vpmaxsw	%xmm1, %xmm2, %xmm2
	vpmaxsw	16(%rax), %xmm9, %xmm9
	vpmaxsd	%xmm1, %xmm2, %xmm2
	vpmaxsd	16(%rax), %xmm9, %xmm9
	vpmaxub	%xmm1, %xmm2, %xmm2
	vpmaxub	16(%rax), %xmm9, %xmm9
	vpmaxuw	%xmm1, %xmm2, %xmm2
	vpmaxuw	16(%rax), %xmm9, %xmm9
	vpmaxud	%xmm1, %xmm2, %xmm2
	vpmaxud	16(%rax), %xmm9, %xmm9
	vpsignb	%xmm1, %xmm2, %xmm2
	vpsignb	16(%rax), %xmm9, %xmm9
	vpsignw	%xmm1, %xmm2, %xmm2
	vpsignw	16(%rax), %xmm9, %xmm9
	vpsignd	%xmm1, %xmm2, %xmm2
	vpsignd	16(%rax), %xmm9, %xmm9
	vphaddw	%xmm1, %xmm2, %xmm2
	vphaddw	16(%rax), %xmm9, %xmm9
	vphaddd	%xmm1, %xmm2, %xmm2
	vphaddd	16(%rax), %xmm9, %xmm9
	vphaddsw	%xmm1, %xmm2, %xmm2
	vphaddsw	16(%rax), %xmm9, %xmm9
	vphsubw	%xmm1, %xmm2, %xmm2
	vphsubw	16(%rax), %xmm9, %xmm9
	vphsubd	%xmm1, %xmm2, %xmm2
	vphsubd	16(%rax), %xmm9, %xmm9
	vphsubsw	%xmm1, %xmm2, %xmm2
	vphsubsw	16(%rax), %xmm9, %xmm9
	vpshufb	%xmm1, %xmm2, %xmm2
	vpshufb	16(%rax), %xmm9, %xmm9
	vpacksswb	%xmm1, %xmm2, %xmm2
	vpacksswb	16(%rax), %xmm9, %xmm9
	vpackssdw	%xmm1, %xmm2, %xmm2
	vpackssdw	16(%rax), %xmm9, %xmm9
	vpackuswb	%xmm1, %xmm2, %xmm2
	vpackuswb	16(%rax), %xmm9, %xmm9
	vpackusdw	%xmm1, %xmm2, %xmm2
	vpackusdw	16(%rax), %xmm9, %xmm9
	vpunpcklbw	%xmm1, %xmm2, %xmm2
	vpunpcklbw	16(%rax), %xmm9, %xmm9
	vpunpcklwd	%xmm1, %xmm2, %xmm2
	vpunpcklwd	16(%rax), %xmm9, %xmm9
	vpunpckldq	%xmm1, %xmm2, %xmm2
	vpunpckldq	16(%rax), %xmm9, %xmm9
	vpunpcklqdq	%xmm1, %xmm2, %xmm2
	vpunpcklqdq	16(%rax), %xmm9, %xmm9
	vpunpckhbw	%xmm1, %xmm2, %xmm2
	vpunpckhbw	16(%rax), %xmm9, %xmm9
	vpunpckhwd	%xmm1, %xmm2, %xmm2
	vpunpckhwd	16(%rax), %xmm9, %xmm9
	vpunpckhdq	%xmm1, %xmm2, %xmm2
	vpunpckhdq	16(%rax), %xmm9, %xmm9
	vpunpckhqdq	%xmm1, %xmm2, %xmm2
	vpunpckhqdq	16(%rax), %xmm9, %xmm9
	vpsllw	$3, %xmm1, %xmm1
	vpsllw	%xmm2, %xmm9, %xmm9
	vpslld	$3, %xmm1, %xmm1
	vpslld	%xmm2, %xmm9, %xmm9
	vpsllq	$3, %xmm1, %xmm1
	vpsllq	%xmm2, %xmm9, %xmm9
	vpsraw	$3, %xmm1, %xmm1
	vpsraw	%xmm2, %xmm9, %xmm9
	vpsrad	$3, %xmm1, %xmm1
	vpsrad	%xmm2, %xmm9, %xmm9
	vpsrlw	$3, %xmm1, %xmm1
	vpsrlw	%xmm2, %xmm9, %xmm9
	vpsrld	$3, %xmm1, %xmm1
	vpsrld	%xmm2, %xmm9, %xmm9
	vpsrlq	$3, %xmm1, %xmm1
	vpsrlq	%xmm2, %xmm9, %xmm9
	vpslldq	$3, %xmm1, %xmm1
	vpsrldq	$3, %xmm1, %xmm1
	vpalignr	$1, %xmm1, %xmm2, %xmm2
	vpalignr	$1, 8(%rax), %xmm9, %xmm9
	vpblendw	$1, %xmm1, %xmm2, %xmm2
	vpblendw	$1, 8(%rax), %xmm9, %xmm9
	vblendps	$1, %xmm1, %xmm2, %xmm2
	vblendps	$1, 8(%rax), %xmm9, %xmm9
	vblendpd	$1, %xmm1, %xmm2, %xmm2
	vblendpd	$1, 8(%rax), %xmm9, %xmm9
	vpinsrb	$1, %eax, %xmm2, %xmm2
	vpinsrb	$1, (%rax), %xmm9, %xmm9
	vpinsrw	$1, %eax, %xmm2, %xmm2
	vpinsrw	$1, (%rax), %xmm9, %xmm9
	vpinsrd	$1, %eax, %xmm2, %xmm2
	vpinsrd	$1, (%rax), %xmm9, %xmm9
	vpinsrq	$1, %rax, %xmm2, %xmm2
	vinsertps	$1, %xmm1, %xmm2, %xmm2
	vinsertps	$1, 8(%rax), %xmm9, %xmm9
	vshufps	$1, %xmm1, %xmm2, %xmm2
	vshufps	$1, 8(%rax), %xmm9, %xmm9
	vshufpd	$1, %xmm1, %xmm2, %xmm2
	vshufpd	$1, 8(%rax), %xmm9, %xmm9
	vunpcklps	%xmm1, %xmm2, %xmm2
	vunpcklps	16(%rax), %xmm9, %xmm9
	vunpcklpd	%xmm1, %xmm2, %xmm2
	vunpcklpd	16(%rax), %xmm9, %xmm9
	vunpckhps	%xmm1, %xmm2, %xmm2
	vunpckhps	16(%rax), %xmm9, %xmm9
	vunpckhpd	%xmm1, %xmm2, %xmm2
	vunpckhpd	16(%rax), %xmm9, %xmm9
	vmovhlps	%xmm1, %xmm2, %xmm2
	vmovlhps	%xmm1, %xmm2, %xmm2
	vaddps	%xmm1, %xmm2, %xmm2
	vaddps	16(%rax), %xmm9, %xmm9
	vaddpd	%xmm1, %xmm2, %xmm2
	vaddpd	16(%rax), %xmm9, %xmm9
	vaddss	%xmm1, %xmm2, %xmm2
	vaddss	16(%rax), %xmm9, %xmm9
	vaddsd	%xmm1, %xmm2, %xmm2
	vaddsd	16(%rax), %xmm9, %xmm9
	vsubps	%xmm1, %xmm2, %xmm2
	vsubps	16(%rax), %xmm9, %xmm9
	vsubpd	%xmm1, %xmm2, %xmm2
	vsubpd	16(%rax), %xmm9, %xmm9
	vsubss	%xmm1, %xmm2, %xmm2
	vsubss	16(%rax), %xmm9, %xmm9
	vsubsd	%xmm1, %xmm2, %xmm2
	vsubsd	16(%rax), %xmm9, %xmm9
	vmulps	%xmm1, %xmm2, %xmm2
	vmulps	16(%rax), %xmm9, %xmm9
	vmulpd	%xmm1, %xmm2, %xmm2
	vmulpd	16(%rax), %xmm9, %xmm9
	vmulss	%xmm1, %xmm2, %xmm2
	vmulss	16(%rax), %xmm9, %xmm9
	vmulsd	%xmm1, %xmm2, %xmm2
	vmulsd	16(%rax), %xmm9, %xmm9
	vdivps	%xmm1, %xmm2, %xmm2
	vdivps	16(%rax), %xmm9, %xmm9
	vdivpd	%xmm1, %xmm2, %xmm2
	vdivpd	16(%rax), %xmm9, %xmm9
	vdivss	%xmm1, %xmm2, %xmm2
	vdivss	16(%rax), %xmm9, %xmm9
	vdivsd	%xmm1, %xmm2, %xmm2
	vdivsd	16(%rax), %xmm9, %xmm9
	vminps	%xmm1, %xmm2, %xmm2
	vminps	16(%rax), %xmm9, %xmm9
	vminpd	%xmm1, %xmm2, %xmm2
	vminpd	16(%rax), %xmm9, %xmm9
	vminss	%xmm1, %xmm2, %xmm2
	vminss	16(%rax), %xmm9, %xmm9
	vminsd	%xmm1, %xmm2, %xmm2
	vminsd	16(%rax), %xmm9, %xmm9
	vmaxps	%xmm1, %xmm2, %xmm2
	vmaxps	16(%rax), %xmm9, %xmm9
	vmaxpd	%xmm1, %xmm2, %xmm2
	vmaxpd	16(%rax), %xmm9, %xmm9
	vmaxss	%xmm1, %xmm2, %xmm2
	vmaxss	16(%rax), %xmm9, %xmm9
	vmaxsd	%xmm1, %xmm2, %xmm2
	vmaxsd	16(%rax), %xmm9, %xmm9
	vandps	%xmm1, %xmm2, %xmm2
	vandps	16(%rax), %xmm9, %xmm9
	vandpd	%xmm1, %xmm2, %xmm2
	vandpd	16(%rax), %xmm9, %xmm9
	vandnps	%xmm1, %xmm2, %xmm2
	vandnps	16(%rax), %xmm9, %xmm9
	vandnpd	%xmm1, %xmm2, %xmm2
	vandnpd	16(%rax), %xmm9, %xmm9
	vorps	%xmm1, %xmm2, %xmm2
	vorps	16(%rax), %xmm9, %xmm9
	vorpd	%xmm1, %xmm2, %xmm2
	vorpd	16(%rax), %xmm9, %xmm9
	vxorps	%xmm1, %xmm2, %xmm2
	vxorps	16(%rax), %xmm9, %xmm9
	vxorpd	%xmm1, %xmm2, %xmm2
	vxorpd	16(%rax), %xmm9, %xmm9
	vhaddps	%xmm1, %xmm2, %xmm2
	vhaddps	16(%rax), %xmm9, %xmm9
	vhaddpd	%xmm1, %xmm2, %xmm2
	vhaddpd	16(%rax), %xmm9, %xmm9
	vhsubps	%xmm1, %xmm2, %xmm2
	vhsubps	16(%rax), %xmm9, %xmm9
	vhsubpd	%xmm1, %xmm2, %xmm2
	vhsubpd	16(%rax), %xmm9, %xmm9
	vsqrtss	%xmm1, %xmm2, %xmm2
	vsqrtss	16(%rax), %xmm9, %xmm9
	vsqrtsd	%xmm1, %xmm2, %xmm2
	vsqrtsd	16(%rax), %xmm9, %xmm9
	vrcpss	%xmm1, %xmm2, %xmm2
	vrcpss	16(%rax), %xmm9, %xmm9
	vrsqrtss	%xmm1, %xmm2, %xmm2
	vrsqrtss	16(%rax), %xmm9, %xmm9
	vroundss	$1, %xmm1, %xmm2, %xmm2
	vroundsd	$1, %xmm1, %xmm2, %xmm2
	vcvtss2sd	%xmm1, %xmm2, %xmm2
	vcvtss2sd	16(%rax), %xmm9, %xmm9
	vcvtsd2ss	%xmm1, %xmm2, %xmm2
	vcvtsd2ss	16(%rax), %xmm9, %xmm9
	vcvtsi2ss	%eax, %xmm2, %xmm2
	vcvtsi2ss	%rax, %xmm9, %xmm9
	vcvtsi2sd	%eax, %xmm2, %xmm2
	vcvtsi2sd	%rax, %xmm9, %xmm9
	vcvtsi2ssl	%eax, %xmm2, %xmm2
	vcvtsi2ssl	4(%rsp), %xmm2, %xmm2
	vcvtsi2ssq	%rax, %xmm2, %xmm2
	vcvtsi2sdl	%eax, %xmm2, %xmm2
	vcvtsi2sdl	4(%rsp), %xmm2, %xmm2
	vcvtsi2sdq	%rax, %xmm2, %xmm2
	vcmpeqps	%xmm1, %xmm2, %xmm2
	vcmpeqps	16(%rax), %xmm9, %xmm9
	vcmpeqpd	%xmm1, %xmm2, %xmm2
	vcmpeqpd	16(%rax), %xmm9, %xmm9
	vcmpeqss	%xmm1, %xmm2, %xmm2
	vcmpeqss	16(%rax), %xmm9, %xmm9
	vcmpeqsd	%xmm1, %xmm2, %xmm2
	vcmpeqsd	16(%rax), %xmm9, %xmm9
	vcmpneqps	%xmm1, %xmm2, %xmm2
	vcmpneqps	16(%rax), %xmm9, %xmm9
	vcmpneqpd	%xmm1, %xmm2, %xmm2
	vcmpneqpd	16(%rax), %xmm9, %xmm9
	vcmpneqss	%xmm1, %xmm2, %xmm2
	vcmpneqss	16(%rax), %xmm9, %xmm9
	vcmpneqsd	%xmm1, %xmm2, %xmm2
	vcmpneqsd	16(%rax), %xmm9, %xmm9
	vcmpltps	%xmm1, %xmm2, %xmm2
	vcmpltps	16(%rax), %xmm9, %xmm9
	vcmpltpd	%xmm1, %xmm2, %xmm2
	vcmpltpd	16(%rax), %xmm9, %xmm9
	vcmpltss	%xmm1, %xmm2, %xmm2
	vcmpltss	16(%rax), %xmm9, %xmm9
	vcmpltsd	%xmm1, %xmm2, %xmm2
	vcmpltsd	16(%rax), %xmm9, %xmm9
	vcmpleps	%xmm1, %xmm2, %xmm2
	vcmpleps	16(%rax), %xmm9, %xmm9
	vcmplepd	%xmm1, %xmm2, %xmm2
	vcmplepd	16(%rax), %xmm9, %xmm9
	vcmpless	%xmm1, %xmm2, %xmm2
	vcmpless	16(%rax), %xmm9, %xmm9
	vcmplesd	%xmm1, %xmm2, %xmm2
	vcmplesd	16(%rax), %xmm9, %xmm9
	vcmpunordps	%xmm1, %xmm2, %xmm2
	vcmpunordps	16(%rax), %xmm9, %xmm9
	vcmpunordpd	%xmm1, %xmm2, %xmm2
	vcmpunordpd	16(%rax), %xmm9, %xmm9
	vcmpunordss	%xmm1, %xmm2, %xmm2
	vcmpunordss	16(%rax), %xmm9, %xmm9
	vcmpunordsd	%xmm1, %xmm2, %xmm2
	vcmpunordsd	16(%rax), %xmm9, %xmm9
	vcmpordps	%xmm1, %xmm2, %xmm2
	vcmpordps	16(%rax), %xmm9, %xmm9
	vcmpordpd	%xmm1, %xmm2, %xmm2
	vcmpordpd	16(%rax), %xmm9, %xmm9
	vcmpordss	%xmm1, %xmm2, %xmm2
	vcmpordss	16(%rax), %xmm9, %xmm9
	vcmpordsd	%xmm1, %xmm2, %xmm2
	vcmpordsd	16(%rax), %xmm9, %xmm9
	vaesenc	%xmm1, %xmm2, %xmm2
	vaesenc	16(%rax), %xmm9, %xmm9
	vaesenclast	%xmm1, %xmm2, %xmm2
	vaesenclast	16(%rax), %xmm9, %xmm9
	vaesdec	%xmm1, %xmm2, %xmm2
	vaesdec	16(%rax), %xmm9, %xmm9
	vaesdeclast	%xmm1, %xmm2, %xmm2
	vaesdeclast	16(%rax), %xmm9, %xmm9
	vpclmulqdq	$1, %xmm1, %xmm2, %xmm2
	vpclmulqdq	$1, 8(%rax), %xmm9, %xmm9
	vmovq	%xmm9, %xmm2
	vmovaps	%xmm9, %xmm2
	vmovups	%xmm9, %xmm2
	vmovapd	%xmm9, %xmm2
	vmovupd	%xmm9, %xmm2
	vmovdqa	%xmm9, %xmm2
	vmovdqu	%xmm9, %xmm2
	vpshufd	$1, %xmm9, %xmm2
	vpshufhw	$1, %xmm9, %xmm2
	vpshuflw	$1, %xmm9, %xmm2
	vptest	%xmm9, %xmm2
	vpabsb	%xmm9, %xmm2
	vpabsw	%xmm9, %xmm2
	vpabsd	%xmm9, %xmm2
	vphminposuw	%xmm9, %xmm2
	vpmovsxbw	%xmm9, %xmm2
	vpmovsxdq	%xmm9, %xmm2
	vpmovzxbw	%xmm9, %xmm2
	vpmovzxbd	%xmm9, %xmm2
	vpmovzxbq	%xmm9, %xmm2
	vpmovzxwd	%xmm9, %xmm2
	vpmovzxwq	%xmm9, %xmm2
	vpmovzxdq	%xmm9, %xmm2
	vsqrtps	%xmm9, %xmm2
	vsqrtpd	%xmm9, %xmm2
	vrcpps	%xmm9, %xmm2
	vrsqrtps	%xmm9, %xmm2
	vroundps	$1, %xmm9, %xmm2
	vroundpd	$1, %xmm9, %xmm2
	vucomiss	%xmm9, %xmm2
	vucomisd	%xmm9, %xmm2
	vcomiss	%xmm9, %xmm2
	vcomisd	%xmm9, %xmm2
	vaesimc	%xmm9, %xmm2
	vaeskeygenassist	$1, %xmm9, %xmm2
	vmovss	%xmm9, %xmm2, %xmm3
	vmovsd	%xmm9, %xmm2, %xmm3
	vpaddb	%xmm9, %xmm2, %xmm3
	vpaddw	%xmm9, %xmm2, %xmm3
	vpaddd	%xmm9, %xmm2, %xmm3
	vpaddq	%xmm9, %xmm2, %xmm3
	vpsubb	%xmm9, %xmm2, %xmm3
	vpsubw	%xmm9, %xmm2, %xmm3
	vpsubd	%xmm9, %xmm2, %xmm3
	vpsubq	%xmm9, %xmm2, %xmm3
	vpaddsb	%xmm9, %xmm2, %xmm3
	vpaddsw	%xmm9, %xmm2, %xmm3
	vpaddusb	%xmm9, %xmm2, %xmm3
	vpaddusw	%xmm9, %xmm2, %xmm3
	vpsubsb	%xmm9, %xmm2, %xmm3
	vpsubsw	%xmm9, %xmm2, %xmm3
	vpsubusb	%xmm9, %xmm2, %xmm3
	vpsubusw	%xmm9, %xmm2, %xmm3
	vpmullw	%xmm9, %xmm2, %xmm3
	vpmulld	%xmm9, %xmm2, %xmm3
	vpmulhw	%xmm9, %xmm2, %xmm3
	vpmulhuw	%xmm9, %xmm2, %xmm3
	vpmulhrsw	%xmm9, %xmm2, %xmm3
	vpmuludq	%xmm9, %xmm2, %xmm3
	vpmuldq	%xmm9, %xmm2, %xmm3
	vpmaddwd	%xmm9, %xmm2, %xmm3
	vpmaddubsw	%xmm9, %xmm2, %xmm3
	vpsadbw	%xmm9, %xmm2, %xmm3
	vpavgb	%xmm9, %xmm2, %xmm3
	vpavgw	%xmm9, %xmm2, %xmm3
	vpand	%xmm9, %xmm2, %xmm3
	vpandn	%xmm9, %xmm2, %xmm3
	vpor	%xmm9, %xmm2, %xmm3
	vpxor	%xmm9, %xmm2, %xmm3
	vpcmpeqb	%xmm9, %xmm2, %xmm3
	vpcmpeqw	%xmm9, %xmm2, %xmm3
	vpcmpeqd	%xmm9, %xmm2, %xmm3
	vpcmpeqq	%xmm9, %xmm2, %xmm3
	vpcmpgtb	%xmm9, %xmm2, %xmm3
	vpcmpgtw	%xmm9, %xmm2, %xmm3
	vpcmpgtd	%xmm9, %xmm2, %xmm3
	vpcmpgtq	%xmm9, %xmm2, %xmm3
	vpminsb	%xmm9, %xmm2, %xmm3
	vpminsw	%xmm9, %xmm2, %xmm3
	vpminsd	%xmm9, %xmm2, %xmm3
	vpminub	%xmm9, %xmm2, %xmm3
	vpminuw	%xmm9, %xmm2, %xmm3
	vpminud	%xmm9, %xmm2, %xmm3
	vpmaxsb	%xmm9, %xmm2, %xmm3
	vpmaxsw	%xmm9, %xmm2, %xmm3
	vpmaxsd	%xmm9, %xmm2, %xmm3
	vpmaxub	%xmm9, %xmm2, %xmm3
	vpmaxuw	%xmm9, %xmm2, %xmm3
	vpmaxud	%xmm9, %xmm2, %xmm3
	vpsignb	%xmm9, %xmm2, %xmm3
	vpsignw	%xmm9, %xmm2, %xmm3
	vpsignd	%xmm9, %xmm2, %xmm3
	vphaddw	%xmm9, %xmm2, %xmm3
	vphaddd	%xmm9, %xmm2, %xmm3
	vphaddsw	%xmm9, %xmm2, %xmm3
	vphsubw	%xmm9, %xmm2, %xmm3
	vphsubd	%xmm9, %xmm2, %xmm3
	vphsubsw	%xmm9, %xmm2, %xmm3
	vpshufb	%xmm9, %xmm2, %xmm3
	vpacksswb	%xmm9, %xmm2, %xmm3
	vpackssdw	%xmm9, %xmm2, %xmm3
	vpackuswb	%xmm9, %xmm2, %xmm3
	vpackusdw	%xmm9, %xmm2, %xmm3
	vpunpcklbw	%xmm9, %xmm2, %xmm3
	vpunpcklwd	%xmm9, %xmm2, %xmm3
	vpunpckldq	%xmm9, %xmm2, %xmm3
	vpunpcklqdq	%xmm9, %xmm2, %xmm3
	vpunpckhbw	%xmm9, %xmm2, %xmm3
	vpunpckhwd	%xmm9, %xmm2, %xmm3
	vpunpckhdq	%xmm9, %xmm2, %xmm3
	vpunpckhqdq	%xmm9, %xmm2, %xmm3
	vpsllw	$3, %xmm9, %xmm2
	vpsllw	%xmm9, %xmm2, %xmm3
	vpslld	$3, %xmm9, %xmm2
	vpslld	%xmm9, %xmm2, %xmm3
	vpsllq	$3, %xmm9, %xmm2
	vpsllq	%xmm9, %xmm2, %xmm3
	vpsraw	$3, %xmm9, %xmm2
	vpsraw	%xmm9, %xmm2, %xmm3
	vpsrad	$3, %xmm9, %xmm2
	vpsrad	%xmm9, %xmm2, %xmm3
	vpsrlw	$3, %xmm9, %xmm2
	vpsrlw	%xmm9, %xmm2, %xmm3
	vpsrld	$3, %xmm9, %xmm2
	vpsrld	%xmm9, %xmm2, %xmm3
	vpsrlq	$3, %xmm9, %xmm2
	vpsrlq	%xmm9, %xmm2, %xmm3
	vpslldq	$3, %xmm9, %xmm2
	vpsrldq	$3, %xmm9, %xmm2
	vpalignr	$1, %xmm9, %xmm2, %xmm3
	vpblendw	$1, %xmm9, %xmm2, %xmm3
	vblendps	$1, %xmm9, %xmm2, %xmm3
	vblendpd	$1, %xmm9, %xmm2, %xmm3
	vinsertps	$1, %xmm9, %xmm2, %xmm3
	vshufps	$1, %xmm9, %xmm2, %xmm3
	vshufpd	$1, %xmm9, %xmm2, %xmm3
	vunpcklps	%xmm9, %xmm2, %xmm3
	vunpcklpd	%xmm9, %xmm2, %xmm3
	vunpckhps	%xmm9, %xmm2, %xmm3
	vunpckhpd	%xmm9, %xmm2, %xmm3
	vmovhlps	%xmm9, %xmm2, %xmm3
	vmovlhps	%xmm9, %xmm2, %xmm3
	vaddps	%xmm9, %xmm2, %xmm3
	vaddpd	%xmm9, %xmm2, %xmm3
	vaddss	%xmm9, %xmm2, %xmm3
	vaddsd	%xmm9, %xmm2, %xmm3
	vsubps	%xmm9, %xmm2, %xmm3
	vsubpd	%xmm9, %xmm2, %xmm3
	vsubss	%xmm9, %xmm2, %xmm3
	vsubsd	%xmm9, %xmm2, %xmm3
	vmulps	%xmm9, %xmm2, %xmm3
	vmulpd	%xmm9, %xmm2, %xmm3
	vmulss	%xmm9, %xmm2, %xmm3
	vmulsd	%xmm9, %xmm2, %xmm3
	vdivps	%xmm9, %xmm2, %xmm3
	vdivpd	%xmm9, %xmm2, %xmm3
	vdivss	%xmm9, %xmm2, %xmm3
	vdivsd	%xmm9, %xmm2, %xmm3
	vminps	%xmm9, %xmm2, %xmm3
	vminpd	%xmm9, %xmm2, %xmm3
	vminss	%xmm9, %xmm2, %xmm3
	vminsd	%xmm9, %xmm2, %xmm3
	vmaxps	%xmm9, %xmm2, %xmm3
	vmaxpd	%xmm9, %xmm2, %xmm3
	vmaxss	%xmm9, %xmm2, %xmm3
	vmaxsd	%xmm9, %xmm2, %xmm3
	vandps	%xmm9, %xmm2, %xmm3
	vandpd	%xmm9, %xmm2, %xmm3
	vandnps	%xmm9, %xmm2, %xmm3
	vandnpd	%xmm9, %xmm2, %xmm3
	vorps	%xmm9, %xmm2, %xmm3
	vorpd	%xmm9, %xmm2, %xmm3
	vxorps	%xmm9, %xmm2, %xmm3
	vxorpd	%xmm9, %xmm2, %xmm3
	vhaddps	%xmm9, %xmm2, %xmm3
	vhaddpd	%xmm9, %xmm2, %xmm3
	vhsubps	%xmm9, %xmm2, %xmm3
	vhsubpd	%xmm9, %xmm2, %xmm3
	vsqrtss	%xmm9, %xmm2, %xmm3
	vsqrtsd	%xmm9, %xmm2, %xmm3
	vrcpss	%xmm9, %xmm2, %xmm3
	vrsqrtss	%xmm9, %xmm2, %xmm3
	vroundss	$1, %xmm9, %xmm2, %xmm3
	vroundsd	$1, %xmm9, %xmm2, %xmm3
	vcmpeqps	%xmm9, %xmm2, %xmm3
	vcmpeqpd	%xmm9, %xmm2, %xmm3
	vcmpeqss	%xmm9, %xmm2, %xmm3
	vcmpeqsd	%xmm9, %xmm2, %xmm3
	vcmpneqps	%xmm9, %xmm2, %xmm3
	vcmpneqpd	%xmm9, %xmm2, %xmm3
	vcmpneqss	%xmm9, %xmm2, %xmm3
	vcmpneqsd	%xmm9, %xmm2, %xmm3
	vcmpltps	%xmm9, %xmm2, %xmm3
	vcmpltpd	%xmm9, %xmm2, %xmm3
	vcmpltss	%xmm9, %xmm2, %xmm3
	vcmpltsd	%xmm9, %xmm2, %xmm3
	vcmpleps	%xmm9, %xmm2, %xmm3
	vcmplepd	%xmm9, %xmm2, %xmm3
	vcmpless	%xmm9, %xmm2, %xmm3
	vcmplesd	%xmm9, %xmm2, %xmm3
	vcmpunordps	%xmm9, %xmm2, %xmm3
	vcmpunordpd	%xmm9, %xmm2, %xmm3
	vcmpunordss	%xmm9, %xmm2, %xmm3
	vcmpunordsd	%xmm9, %xmm2, %xmm3
	vcmpordps	%xmm9, %xmm2, %xmm3
	vcmpordpd	%xmm9, %xmm2, %xmm3
	vcmpordss	%xmm9, %xmm2, %xmm3
	vcmpordsd	%xmm9, %xmm2, %xmm3
	vaesenc	%xmm9, %xmm2, %xmm3
	vaesenclast	%xmm9, %xmm2, %xmm3
	vaesdec	%xmm9, %xmm2, %xmm3
	vaesdeclast	%xmm9, %xmm2, %xmm3
	vpclmulqdq	$1, %xmm9, %xmm2, %xmm3
