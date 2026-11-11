/* Runtime operands deliberately arrive as bytes, including dirty padding.
 * The oracle lives in bench/vcc-bitint.py and does not use compiler BitInt. */
typedef unsigned _BitInt(WIDTH) U;
typedef _BitInt(WIDTH) S;
enum { WORDS = (WIDTH+63)/64 };
#define PUT(i, expression) do { U value = (U)(expression); __builtin_memcpy(out+(i)*WORDS, &value, sizeof value); } while (0)
void wide_ops(const unsigned long long *pa, const unsigned long long *pb,
              unsigned shift, unsigned long long *out, unsigned *cmp)
{
    U a,b; S sa,sb;
    __builtin_memcpy(&a,pa,sizeof a); __builtin_memcpy(&b,pb,sizeof b);
    __builtin_memcpy(&sa,pa,sizeof sa); __builtin_memcpy(&sb,pb,sizeof sb);
    PUT(0,a+b); PUT(1,a-b); PUT(2,a*b);
    PUT(3,a&b); PUT(4,a|b); PUT(5,a^b);
    PUT(6,a<<shift); PUT(7,a>>shift); PUT(8,a/b); PUT(9,a%b);
    PUT(10,-a); PUT(11,~a); PUT(12,sa>>shift);
    PUT(13,sa/sb); PUT(14,sa%sb);
    *cmp = (a==b) | ((a<b)<<1) | ((a>b)<<2) | ((sa<sb)<<3) |
           ((sa>sb)<<4) | ((!a)<<5) | ((!sa)<<6);
}

static struct { U before, value, after; } indirect;
static unsigned pointer_calls, rhs_calls;
__attribute__((noinline)) static U *wide_pointer(void)
{
    pointer_calls++;
    return &indirect.value;
}
__attribute__((noinline)) static U wide_rhs(void)
{
    rhs_calls++;
    return (U)1 << (WIDTH-2);
}

unsigned wide_control(void)
{
    volatile U zero = 0;
    U a=zero+7, b=++a;
    if (a!=8 || b!=8) return 1;
    b=a++; if (a!=9 || b!=8) return 2;
    b=--a; if (a!=8 || b!=8) return 3;
    b=a--; if (a!=7 || b!=8) return 4;
    unsigned _BitInt(WIDTH+3) wider=zero+11;
    a+=wider; a*=3; a-=2; a/=4; a%=7;
    if(a!=6) return 5;
    a<<=65; a>>=65; if(a!=6) return 6;
    int array[4]={3,5,11,17}; U index=zero+2;
    if(array[index]!=11 || *(array+index)!=11) return 7;
    a=(U)1<<(WIDTH-1);
    if(!a || !(_Bool)a || (_Bool)zero) return 8;
    b=zero ? (U)2 : a; if(b!=a) return 9;
    b=a ? a : (U)2; if(b!=a) return 10;
    if ((unsigned long long)(a+13)!=13) return 11;
    struct __attribute__((packed)) { unsigned p:3; U x:WIDTH-3; unsigned q:5; } packed={0};
    packed.p=5; packed.q=17; packed.x=zero+37;
    packed.x+=5; b=packed.x++;
    if(b!=42 || packed.x!=43 || packed.p!=5 || packed.q!=17) return 12;
    packed.x=~zero;
    if(packed.x!=((~(U)0)>>3) || packed.p!=5 || packed.q!=17) return 13;
    struct __attribute__((packed)) { unsigned p:3; S x:WIDTH-7; unsigned q:5; } signed_packed={0};
    signed_packed.p=6; signed_packed.q=19; signed_packed.x=(S)zero-7;
    signed_packed.x++;
    if(signed_packed.x!=-6 || signed_packed.p!=6 || signed_packed.q!=19) return 14;
    a=(U)1<<(WIDTH-1); b=0;
    switch(a) {case 0:b=1;break;case 1 ... 3:b=2;break;default:b=7;break;}
    if(b!=7) return 15;
    a=zero+2;
    switch(a) {case 0:b=1;break;case 1 ... 3:b=2;break;default:b=7;break;}
    if(b!=2) return 16;
    U count=zero+65;
    a=zero+6; b=(a<<count)>>count; if(b!=6) return 17;
    a<<=count; a>>=count; if(a!=6) return 18;
    unsigned long long raw[WORDS];
    for(unsigned i=0;i<WORDS;i++) raw[i]=~0ULL;
    raw[0]-=15;
#if WIDTH%64
    raw[WORDS-1]=(1ULL<<(WIDTH%64))-1;
#endif
    S negative; __builtin_memcpy(&negative,raw,sizeof negative);
    negative>>=count; if(negative!=-1) return 19;
    struct __attribute__((packed)) {
        unsigned p:3; S s1:1; U u7:7; S s64:64; U u65:65; unsigned q:5;
    } small={0};
    small.p=5; small.q=23; small.s1=-1; small.u7=97; small.s64=-17;
    small.u65=((U)1<<64)+3;
    if(small.s1!=-1 || small.u7!=97 || small.s64!=-17 || small.u65!=(((U)1<<64)+3)) return 20;
    small.s64++; small.u65++;
    if(small.s64!=-16 || small.u65!=(((U)1<<64)+4) || small.p!=5 || small.q!=23) return 21;
    negative=(S)zero-2;
    switch(negative) {case -3 ... -1:b=11;break;case 0:b=1;break;default:b=0;break;}
    if(b!=11) return 22;
    indirect.before=13; indirect.after=29; indirect.value=7;
    pointer_calls=rhs_calls=0;
    U high=wide_rhs();
    *wide_pointer()+=high;
    if(indirect.value!=high+7 || pointer_calls!=1 || rhs_calls!=1) return 23;
    indirect.value=7;
    *wide_pointer()+=wide_rhs();
    if(indirect.value!=high+7 || pointer_calls!=2 || rhs_calls!=2) return 24;
    b=(*wide_pointer())++;
    if(b!=high+7 || indirect.value!=high+8 || pointer_calls!=3) return 25;
    b=++*wide_pointer();
    if(b!=high+9 || indirect.value!=high+9 || pointer_calls!=4) return 26;
    b=(*wide_pointer())--;
    if(b!=high+9 || indirect.value!=high+8 || pointer_calls!=5) return 27;
    b=--*wide_pointer();
    if(b!=high+7 || indirect.value!=high+7 || pointer_calls!=6) return 28;
    if(indirect.before!=13 || indirect.after!=29 || rhs_calls!=2) return 29;
    return 0;
}
