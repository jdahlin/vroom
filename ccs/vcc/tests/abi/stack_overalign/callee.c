typedef unsigned long word; struct __attribute__((aligned(64))) block { word x[8]; };
word probe(struct block b, word a, word c, word d, word e, word f, word g, word tail) {
 _Alignas(256) volatile char local[33];
 local[0]=tail;
 if ((word)local % 256 || (word)&b % _Alignof(struct block)) return 999;
 return b.x[0]+b.x[7]+a+c+d+e+f+g+tail+local[0];
}
