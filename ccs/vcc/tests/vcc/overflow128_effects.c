/* Runtime operand/result-pointer calls are each evaluated once; the result is
 * stored even on overflow.  Each callback has independent state. */
typedef unsigned __int128 U;
typedef __int128 S;
static int ca, cb, cp;
static U result;
__attribute__((noinline)) static U a(void) { ca++; return ~(U)0; }
__attribute__((noinline)) static U b(void) { cb++; return 2; }
__attribute__((noinline)) static U *p(void) { cp++; return &result; }
__attribute__((noinline)) static int product(S a, S b, S *r) {
    return __builtin_mul_overflow(a,b,r);
}
int main(void) {
    int o=__builtin_mul_overflow(a(),b(),p());
    if(!o || ca!=1 || cb!=1 || cp!=1 || result!=~(U)1) return 1;
    ca=cb=cp=0;
    o=__builtin_add_overflow(a(),b(),p());
    if(!o || ca!=1 || cb!=1 || cp!=1 || result!=1) return 2;
    ca=cb=cp=0;
    o=__builtin_sub_overflow(b(),a(),p());
    if(!o || ca!=1 || cb!=1 || cp!=1 || result!=3) return 3;
    S minimum=(S)((U)1<<127), value;
    if(!product(minimum,-1,&value) || value!=minimum) return 4;
    if(product(minimum,1,&value) || value!=minimum) return 5;
    return 0;
}
