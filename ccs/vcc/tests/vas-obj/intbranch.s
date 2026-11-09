.text
b 8
bl 0x100
b.eq 12
cbz x0, 16
tbz x0, #3, 20
adr x0, 4
adr x1, -8
adrp x0, 4096
ldr x0, 64
