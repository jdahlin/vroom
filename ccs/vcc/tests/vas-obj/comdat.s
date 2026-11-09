.text
nop
.section .text.f,"axG",@progbits,f,comdat
.globl f
f: bl g
.section .data.f,"awG",@progbits,f,comdat
.quad f
.section .text.g,"axG",@progbits,g
g: ret
.section .rodata.h,"aMG",@progbits,4,hh,comdat
.long 1
