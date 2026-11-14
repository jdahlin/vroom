# Re-enter cached section roots after their endpoints have split again.
.text
.globl continuations
continuations:
.rept 24
  jmp 1f
  .p2align 4
  .text
  nop
  .pushsection .text.other,"ax",@progbits
  jmp 2f
  .p2align 5
  .popsection
  .p2align 3
1:
  .pushsection .text.other,"ax",@progbits
2:
  nop
  .popsection
  .data
  .quad continuations
  .previous
.endr
ret
