.text
.globl negate_min
negate_min:
 movabsq $-9223372036854775808, %r11
 movabsq $-0x8000000000000000, %rax
 retq
.data
 .quad -9223372036854775808
 .quad -(-0x8000000000000000)
 .quad ~(-0x8000000000000000)
