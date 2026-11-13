# Interleaved sections and subsections: fragment ids are not layout order.
.text
.file 0 "/test" "relax.c"
.file 1 "/test" "relax.c"
.globl indexed_relax
indexed_relax:
.cfi_startproc
.Lbegin:
  .loc 1 10 0
  jmp .Llast
  .rept 40
  jne .Llast
  .p2align 4,,7
  .endr
  .uleb128 .Llast-.Lbegin
  .sleb128 .Lbegin-.Llast
  .subsection 2
.Llast:
  .loc 1 20 0
  jne .Lbegin
  ret
.cfi_endproc
  .subsection 1
  .rept 20
  jmp .Llast
  .p2align 5
  .endr
.pushsection .text.other,"ax",@progbits
.Lother:
  jmp .Lother_end
  .skip 129
  .p2align 4
.Lother_end:
  jne .Lother
.popsection
  .subsection 0
  .p2align 4
  jmp .Llast
