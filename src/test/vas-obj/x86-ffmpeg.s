.text
cdq
fstenv 16(%r9)
leal 4(%r8d,%edx,2), %esi
movzb (%r12,%r14), %r14
movd %r9d, %mm6
movq (%rax), %mm2
movq %mm2, %mm3
movq %mm2, (%r9)
pxor %mm7, %mm7
psllw $5, %mm1
pmaddwd 16(%r8), %mm4
pshufw $0xfa, %mm4, %mm6
movntq %mm1, (%rdi)
movntdq %xmm10, (%rdi)
psignw %xmm1, %xmm0
