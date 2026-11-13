# Lexer encodings, including high ZMM registers, mask zeroing and disp8 scaling.
vbroadcasti32x4 (%rax),%zmm2
vmovdqu64 (%rax),%zmm0
vmovdqa64 %zmm0,%zmm14
vpermt2b %zmm1,%zmm12,%zmm14
vpermb %zmm4,%zmm13,%zmm15
vpandq %zmm7,%zmm12,%zmm17
vpshufb %zmm17,%zmm2,%zmm17
vpsrlw $4,%zmm12,%zmm18
vptestmb %zmm8,%zmm14,%k0
kmovq %k0,(%rax)
vpcmpeqb %zmm12,%zmm13,%k1
vpmovb2m %zmm12,%k0
vporq %zmm12,%zmm14,%zmm12
kandq %k1,%k2,%k0
korq %k0,%k1,%k0
knotq %k0,%k1
vpcmpub $1,%zmm6,%zmm13,%k1
vpermb %zmm4,%zmm13,%zmm15 {%k1} {z}
vpbroadcastb (%rax),%zmm6
vmovdqu64 -64(%r12,%r9,2),%zmm31
vmovdqu64 63(%r13),%zmm16
vbroadcasti32x4 2032(%rax),%zmm2
vptestmb %zmm31,%zmm23,%k7
vpmovmskb %ymm12,%r8d
vpminub %ymm3,%ymm2,%ymm5
