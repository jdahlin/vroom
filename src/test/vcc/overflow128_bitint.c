/* vcc-xfail-wasm: TODO.md _BitInt widths above 128 and remaining targets: wasm runtime precision and ABI not implemented. */
/* Full-precision overflow with BitInt operands and destinations.
 * Inputs reach noinline functions at runtime; compare the complete result and
 * overflow flag with the reference compiler via a deterministic digest. */
#include <stdio.h>
typedef unsigned __int128 U;
typedef __int128 S;
static unsigned long long digest = 0x123456789abcdefULL;
static void record(int overflow, U value)
{
    digest = digest * 6364136223846793005ULL + (unsigned long long)value;
    digest ^= (unsigned long long)(value >> 64);
    digest = digest * 1442695040888963407ULL + overflow;
}
#define CASE(N,A,B,R) \
__attribute__((noinline)) static void test##N(A a, B b) { \
    R r; int o; \
    o = __builtin_add_overflow(a,b,&r); record(o,(U)r); \
    o = __builtin_sub_overflow(a,b,&r); record(o,(U)r); \
    o = __builtin_mul_overflow(a,b,&r); record(o,(U)r); \
    o = __builtin_sub_overflow(a,b,&a); record(o,(U)a); \
}
CASE(8,_BitInt(70),_BitInt(80),_BitInt(65))
CASE(9,unsigned _BitInt(127),_BitInt(128),unsigned _BitInt(70))
CASE(10,unsigned _BitInt(65),unsigned _BitInt(65),_BitInt(127))
CASE(11,_BitInt(128),unsigned _BitInt(127),_BitInt(31))
CASE(12,unsigned long,unsigned long,_BitInt(70))
CASE(13,long,long,unsigned _BitInt(127))
CASE(14,U,S,_BitInt(2))
CASE(15,S,U,unsigned _BitInt(1))
CASE(17,S,U,_BitInt(64))
CASE(18,U,S,unsigned _BitInt(31))
static void test(U a,U b)
{
#define RUN(N) test##N(a,b)

    RUN(8);RUN(9);RUN(10);RUN(11);RUN(12);RUN(13);RUN(14);RUN(15);RUN(17);RUN(18);
}
int main(void)
{
    const U values[] = {0,1,2,3,127,128,255,256, (U)1<<31,
        ((U)1<<63)-1,(U)1<<63,((U)1<<64)-1,(U)1<<64,
        ((U)1<<65)-1,(U)1<<69,(U)1<<79,((U)1<<126)-1,
        (U)1<<126,((U)1<<127)-1,(U)1<<127,~(U)0,~(U)1};
    for (unsigned i=0;i<sizeof(values)/sizeof(*values);i++)
        for (unsigned j=0;j<sizeof(values)/sizeof(*values);j++) test(values[i],values[j]);
    unsigned long long state=17;
    for (unsigned i=0;i<200;i++) {
        state=state*6364136223846793005ULL+1; U a=(U)state<<64;
        state=state*6364136223846793005ULL+1; a|=state;
        state=state*6364136223846793005ULL+1; U b=(U)state<<64;
        state=state*6364136223846793005ULL+1; b|=state;
        test(a,b);
    }
    printf("%016llx\n",digest);
    return 0;
}
