.text
nop
.reloc ., R_AARCH64_NONE, sym
nop
.reloc 0, BFD_RELOC_NONE, sym2
.data
.quad 0
.reloc .-8, R_AARCH64_ABS64, sym+8
