.macro m a
nop
bogus \a
.endm
.macro n
m x3
.endm
nop
m x1
n
.rept 2
nop
bad
.endr
