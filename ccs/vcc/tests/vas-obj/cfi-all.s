.file "cfi.c"
.text
.globl f
f:
.cfi_startproc
.cfi_b_key_frame
.cfi_negate_ra_state
stp x29, x30, [sp, #-32]!
.cfi_def_cfa_offset 32
.cfi_offset w30, -24
.cfi_offset w29, -32
.cfi_offset d8, -8
mov x29, sp
.cfi_def_cfa_register x29
.cfi_remember_state
.cfi_rel_offset x19, 16
.cfi_adjust_cfa_offset 16
.cfi_register x20, x21
.cfi_undefined x22
.cfi_same_value x23
.cfi_escape 0x10, 0x1f, 0x02
.zero 400
.cfi_restore_state
.cfi_restore x30
.cfi_restore d8
ldp x29, x30, [sp], #32
.cfi_def_cfa sp, 0
ret
.cfi_endproc
g:
.cfi_startproc simple
.cfi_signal_frame
.cfi_return_column 29
.zero 70000
ret
.cfi_endproc
h:
.cfi_startproc
ret
.cfi_endproc
