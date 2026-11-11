.cfi_sections .debug_frame
.text
f: .cfi_startproc
pushq %rbp
.cfi_adjust_cfa_offset 8
.cfi_rel_offset %rbp,0
ret
.cfi_endproc
g: .cfi_startproc
ret
.cfi_endproc
