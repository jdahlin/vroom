.globl f,h,k
f: ret
h: ret
k: ret
.symver f,f@V1
.symver h,h2@@V2
.symver k,k@V3,remove
bl f
bl k
.data
.quad h
