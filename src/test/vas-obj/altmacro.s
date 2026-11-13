.altmacro
.macro m a, b
.long \a
.long b
.endm
m %1+2, 7
m <3>, <4>
.macro str s
.ascii "s"
.endm
str <x!>y>
.noaltmacro
.macro n a
.long \a
.endm
n 5
