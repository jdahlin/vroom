.text
nop
jmp 2f
.skip 200
2: ret
.org 0x300, 0xcc
x: ret
3: jmp 4f
.skip 300
4: ud2
.org 3b + 320, 0xcc
nop
