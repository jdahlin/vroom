#include "types.h"
Big relay(Big a,long tag,Big b) {
    unsigned char *p=(void*)&a;
    const unsigned char *q=(const void*)&b;
    p[0]^=q[0]; p[1]^=(unsigned char)tag;
    return a;
}
Big pressure(long a,long b,long c,long d,long e,long f,long g,long h,Big x,long tag) {
    unsigned char *p=(void*)&x;
    p[0]^=(unsigned char)(a+b+c+d+e+f+g+h+tag);
    return x;
}
Big variadic(int n,...) {
    __builtin_va_list ap;
    __builtin_va_start(ap,n);
    Big a=__builtin_va_arg(ap,Big);
    long middle=__builtin_va_arg(ap,long);
    Big b=__builtin_va_arg(ap,Big);
    __builtin_va_end(ap);
    return relay(a,middle+n,b);
}
Wrapped wrap(Wrapped x) { x.lead+=2; x.tail-=3;return x; }
__attribute__((noinline)) static int address_bits(const void *p) {
    __asm__("" : "+r"(p));
    return (unsigned long)p & (TYPE_ALIGN-1);
}
int parameter_alignment(Big x) { return address_bits(&x); }
Big variadic_stack(long a,long b,long c,long d,long e,long f,long g,long h,...) {
    __builtin_va_list ap;
    __builtin_va_start(ap,h);
    Big x=__builtin_va_arg(ap,Big);
    long middle=__builtin_va_arg(ap,long);
    Big y=__builtin_va_arg(ap,Big);
    __builtin_va_end(ap);
    return relay(x,middle+a+b+c+d+e+f+g+h,y);
}
