# The same mnemonic selects a different encoder for each register class.
.text
movq %rax, %r9
movq %xmm1, %rax
movq %rax, %mm0
movq %cr3, %rax
movq %rax, %ds
# Scalar addresses, immediate widths and implicit operands.
movq target(%rip), %rax
addl $1, target(%rip)
subq %rax, target(%rip)
imulq %r9
imulq (%rax), %r9
imulq $257, (%rax), %r9
pushq %r9
popq %r9
pushq target(%rip)
