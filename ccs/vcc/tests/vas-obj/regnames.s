.text
ldursw x1, [x29, #-8]
ldursh w2, [x0, #3]
ldursh x2, [x0, #-256]
ldursb w3, [sp]
ldursb x3, [x1, #255]
adrp x8, v1
add x8, x8, :lo12:v1
adrp x9, x1
.data
v1: .quad 0
x1: .quad 0
q0: s2: b3: d4: .quad 0
