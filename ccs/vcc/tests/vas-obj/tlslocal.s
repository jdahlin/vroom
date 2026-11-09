.text
mrs x8, tpidr_el0
add x8, x8, :tprel_hi12:t
add x8, x8, :tprel_lo12_nc:t
adrp x0, :gottprel:u
ldr x0, [x0, :gottprel_lo12:u]
adrp x1, :got:d
ldr x1, [x1, :got_lo12:d]
.section .tbss,"awT",@nobits
.p2align 2
t: .zero 4
.section .tdata,"awT",@progbits
.globl u
u: .long 5
.data
d: .quad 0
