#define ENTRY(x) .globl x; x:
#define SYM(n) n ## _end
ENTRY(foo)
 mov x0, #1 // comment
# 12 "x.S"
.ifdef X
#unknown directive
SYM(bar): .quad 1f - .
