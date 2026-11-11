.text
vpternlogq $256,%zmm1,%zmm2,%zmm3
vpternlogq $-129,%zmm1,%zmm2,%zmm3
vpternlogq %eax,%zmm1,%zmm2,%zmm3
kmovq %k1,%rax{%k1}
movq %k1,%rax
vmovdqu8 %zmm4294967296,%zmm0
vmovdqu8 %zmm00,%zmm0
