.weakref a1, t1
.weakref a2, t2
.weakref a3, t3
.globl t3
.text
bl a1
bl t2
bl a2
bl a3
t3: ret
.data
.quad a1
