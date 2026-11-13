.text
.globl f
.type f,@function
f: ret
.size f,4
.symver f,f@VER_1
.symver f,f@@VER_2
.weak g
g: ret
.symver g,g@@@VER_3
.symver u,u@VER_4
bl u
.symver f,ff@VER_5,remove
