/* vcc-flags: -std=c23 */
/* vcc-xfail-wasm: TODO.md _BitInt remaining targets: runtime precision not implemented. */
#define L(x,n) __builtin_stdc_rotate_left(x,n)
#define R(x,n) __builtin_stdc_rotate_right(x,n)
typedef unsigned __int128 U128;
#define TEST(T,W,B,NAME) \
_Static_assert(_Generic(L((T)0,0), T:1, default:0)); \
_Static_assert(_Generic(R((T)0,0), T:1, default:0)); \
__attribute__((noinline)) static int NAME(void) { \
    volatile T input = ((T)1 << ((B)-1)) | 1; \
    volatile U128 counts[] = {0,1,(B)-1,(B),2*(B),((U128)1<<100)+17}; \
    for (unsigned i=0;i<sizeof(counts)/sizeof(*counts);++i) { \
        T x=input; U128 n=counts[i]; unsigned k=n%(B), back=((B)-k)%(B); \
        T left=(T)(((W)x<<k)|((W)x>>back)); \
        T right=(T)(((W)x>>k)|((W)x<<back)); \
        if (L(x,n)!=left || R(x,n)!=right) return 1; \
    } \
    T x=1; unsigned n=1; \
    if(L(x++,n++)!=(T)((B)==1?1:2) || x!=(T)2 || n!=2) return 2; \
    x=1; n=1; \
    if(R(x++,n++)!=(T)((T)1<<((B)-1)) || x!=(T)2 || n!=2) return 3; \
    return 0; \
}
TEST(unsigned char,U128,8,t8)
TEST(unsigned short,U128,16,t16)
TEST(unsigned,U128,32,t32)
TEST(unsigned long long,U128,64,t64)
TEST(U128,U128,128,t128)
TEST(unsigned _BitInt(1),U128,1,b1)
TEST(unsigned _BitInt(7),U128,7,b7)
TEST(unsigned _BitInt(59),U128,59,b59)
TEST(unsigned _BitInt(65),U128,65,b65)
TEST(unsigned _BitInt(127),U128,127,b127)
#if __BITINT_MAXWIDTH__ >= 510
#define WIDE(T,B,NAME) \
_Static_assert(_Generic(L((T)0,0), T:1, default:0)); \
_Static_assert(_Generic(R((T)0,0), T:1, default:0)); \
__attribute__((noinline)) static int NAME(void) { \
    volatile T input=((T)1<<((B)-1))|3; T x=input; \
    if(L(x,0)!=x || R(x,(B))!=x || L(x,2*(B))!=x) return 1; \
    if(L(x,(B)-2)!=((x<<((B)-2))|(x>>2))) return 2; \
    if(R(x,(B)+1)!=((x>>1)|(x<<((B)-1)))) return 3; \
    T v=1; if(L(v++,1)!=2 || v!=2) return 4; \
    v=1; if(R(v++,1)!=((T)1<<((B)-1)) || v!=2) return 5; \
    return 0; \
}
WIDE(unsigned _BitInt(129),129,b129)
WIDE(unsigned _BitInt(510),510,b510)
/* Constant wide counts can be folded by the Clang oracle; its dynamic
   >128-bit remainder lowering is unavailable. Keep the high count bits. */
#define COUNT (((unsigned _BitInt(257))1<<200)+17)
__attribute__((noinline)) static int wide_count(void) {
    volatile unsigned _BitInt(510) input=((unsigned _BitInt(510))1<<509)|3;
    unsigned _BitInt(510) x=input;
    if (L(x,COUNT)!=((x<<273)|(x>>237)) || R(x,COUNT)!=((x>>273)|(x<<237))) return 1;
    volatile unsigned _BitInt(7) small=65;
    if (L(small,COUNT)!=65 || R(small,COUNT)!=65) return 2;
    return 0;
}
#endif
int main(void) {
    if(t8()||t16()||t32()||t64()||t128()||b1()||b7()||b59()||b65()||b127()) return 1;
#if __BITINT_MAXWIDTH__ >= 510
    if(b129()||b510()||wide_count()) return 2;
#endif
    return 0;
}
