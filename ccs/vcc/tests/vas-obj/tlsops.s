.text
adrp x0, :tlsdesc:v
ldr x1, [x0, :tlsdesc_lo12:v]
add x0, x0, :tlsdesc_lo12:v
.tlsdesccall v
blr x1
adrp x0, :gottprel:v
ldr x0, [x0, :gottprel_lo12:v]
ldr x0, :gottprel:v
movz x0, :gottprel_g1:v
movk x0, :gottprel_g0_nc:v
movz x0, :tprel_g2:v
movk x0, :tprel_g1_nc:v
movz x0, :tprel_g1:v
movk x0, :tprel_g0_nc:v
movz x0, :tprel_g0:v
add x0, x0, :tprel_hi12:v
add x0, x0, :tprel_lo12_nc:v
add x0, x0, :tprel_lo12:v
ldrb w0, [x0, :tprel_lo12:v]
ldrh w0, [x0, :tprel_lo12_nc:v]
ldr w0, [x0, :tprel_lo12:v]
ldr x0, [x0, :tprel_lo12_nc:v]
ldr q0, [x0, :tprel_lo12:v]
movz x0, :dtprel_g2:v
movk x0, :dtprel_g1_nc:v
movz x0, :dtprel_g1:v
movk x0, :dtprel_g0_nc:v
movz x0, :dtprel_g0:v
add x0, x0, :dtprel_hi12:v
add x0, x0, :dtprel_lo12:v
add x0, x0, :dtprel_lo12_nc:v
ldrb w0, [x0, :dtprel_lo12:v]
ldr x0, [x0, :dtprel_lo12_nc:v]
ldr x0, :got:g
ldr x0, :tlsdesc:v
movz x0, :prel_g3:g
movk x0, :prel_g2_nc:g
movz x0, :prel_g1:g
movk x0, :prel_g0_nc:g
movz x0, :prel_g0:g
adrp x0, :got:l
ldr x0, [x0, :got_lo12:l]
adrp x0, :pg_hi21_nc:g
l: ret
