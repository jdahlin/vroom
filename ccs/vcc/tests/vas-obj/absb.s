.set X, 0x100
.text
nop
b X
bl X+4
cbz x0, X
