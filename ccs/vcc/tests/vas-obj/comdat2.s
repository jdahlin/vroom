.section .text.k,"axG",@progbits,k,comdat
nop
.text
nop
.section .text.k,"axG",@progbits,k,comdat
.weak k
k: ret
.section .bss.k,"awG",@nobits,k,comdat
.zero 8
.section .text.z,"axG",%progbits,zz
