/* Unsigned64 quotient/remainder, fixed-register preservation and effects. */
#define NI __attribute__((noinline))
typedef unsigned long long U;
static volatile U observed;
static volatile unsigned effects, index;
static NI U next(U x) { effects++; return x; }
NI U u64_q3(U n) { return n/3ULL; }
NI U u64_r3(U n) { return n%3ULL; }
NI U u64_q5(U n) { return n/5ULL; }
NI U u64_r5(U n) { return n%5ULL; }
NI U u64_q7(U n) { return n/7ULL; }
NI U u64_r7(U n) { return n%7ULL; }
NI U u64_q10(U n) { return n/10ULL; }
NI U u64_r10(U n) { return n%10ULL; }
NI U u64_q11(U n) { return n/11ULL; }
NI U u64_r11(U n) { return n%11ULL; }
NI U u64_q14(U n) { return n/14ULL; }
NI U u64_r14(U n) { return n%14ULL; }
NI U u64_q19(U n) { return n/19ULL; }
NI U u64_r19(U n) { return n%19ULL; }
NI U u64_q37(U n) { return n/37ULL; }
NI U u64_r37(U n) { return n%37ULL; }
NI U u64_q63(U n) { return n/63ULL; }
NI U u64_r63(U n) { return n%63ULL; }
NI U u64_q4294967295(U n) { return n/4294967295ULL; }
NI U u64_r4294967295(U n) { return n%4294967295ULL; }
NI U u64_q4294967297(U n) { return n/4294967297ULL; }
NI U u64_r4294967297(U n) { return n%4294967297ULL; }
NI U u64_q9223372036854775807(U n) { return n/9223372036854775807ULL; }
NI U u64_r9223372036854775807(U n) { return n%9223372036854775807ULL; }
NI U u64_q9223372036854775809(U n) { return n/9223372036854775809ULL; }
NI U u64_r9223372036854775809(U n) { return n%9223372036854775809ULL; }
NI U u64_q18446744073709551615(U n) { return n/18446744073709551615ULL; }
NI U u64_r18446744073709551615(U n) { return n%18446744073709551615ULL; }
NI unsigned long oa_div10(unsigned long n) { return n/10UL; }
NI U u64_compound(U *p) { return *p /= 7ULL; }
NI U u64_compoundrem(U *p) { return *p %= 10ULL; }
NI U u64_effect(U x) { return next(x)/10ULL; }
NI U u64_effectrem(U x) { return next(x)%7ULL; }
NI U u64_rhs(U x) { return x/(next(0),10ULL); }
NI U u64_rhsrem(U x) { return x%(next(0),7ULL); }
NI U u64_volatile(void) { return observed/10ULL; }
NI U u64_volatile_compound(void) { return observed/=7ULL; }
NI U u64_volatile_rem(void) { return observed%=10ULL; }
NI U u64_index(U *p) { return p[index++]/=7ULL; }
NI U u64_negative(U x) { return x/-3LL; }
NI U u64_dynamic(U x,U d) { return x/d; }
NI long long u64_signed(long long n) { return n/7LL; }
static U anchor;
static NI U pressure_next(U n) { return n+observed; }
NI U u64_pressure(void) { return pressure_next(14ULL)+(pressure_next(13ULL)+(pressure_next(12ULL)+(pressure_next(11ULL)+(pressure_next(10ULL)+(pressure_next(9ULL)+(pressure_next(8ULL)+(pressure_next(7ULL)+(pressure_next(6ULL)+(pressure_next(5ULL)+(pressure_next(4ULL)+(pressure_next(3ULL)+(pressure_next(2ULL)+(pressure_next(1ULL)+(pressure_next(0ULL)+(((U)(unsigned long)&anchor % 7ULL)))))))))))))))); }
NI U u64_fixed(U x,U seed)
{
#if defined(__x86_64__)
    register U ax __asm__("rax")=seed;
    register U dx __asm__("rdx")=seed^0xfedcba9876543210ULL;
    __asm__ volatile ("" : "+a"(ax), "+d"(dx));
    U q=x/7ULL, r=x%10ULL;
    __asm__ volatile ("" : "+a"(ax), "+d"(dx) : "r"(q), "r"(r));
    return q+r+ax+dx;
#else
    return x/7ULL+x%10ULL+seed+(seed^0xfedcba9876543210ULL);
#endif
}
static unsigned seed=0x13579bdf;
static unsigned random32(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
int main(void)
{
    const U edges[]={0,1,2,3,4294967295ULL,4294967296ULL,0x7fffffffffffffffULL,
        0x8000000000000000ULL,0xfffffffffffffffeULL,0xffffffffffffffffULL};
    for (unsigned i=0;i<256;i++) {
        U n=i<sizeof edges/sizeof *edges ? edges[i] : ((U)random32()<<32)|random32();
        if(u64_q3(n)!=u64_dynamic(n,3ULL) || u64_r3(n)!=n-u64_dynamic(n,3ULL)*3ULL) return 1;
        if(u64_q5(n)!=u64_dynamic(n,5ULL) || u64_r5(n)!=n-u64_dynamic(n,5ULL)*5ULL) return 1;
        if(u64_q7(n)!=u64_dynamic(n,7ULL) || u64_r7(n)!=n-u64_dynamic(n,7ULL)*7ULL) return 1;
        if(u64_q10(n)!=u64_dynamic(n,10ULL) || u64_r10(n)!=n-u64_dynamic(n,10ULL)*10ULL) return 1;
        if(u64_q11(n)!=u64_dynamic(n,11ULL) || u64_r11(n)!=n-u64_dynamic(n,11ULL)*11ULL) return 1;
        if(u64_q14(n)!=u64_dynamic(n,14ULL) || u64_r14(n)!=n-u64_dynamic(n,14ULL)*14ULL) return 1;
        if(u64_q19(n)!=u64_dynamic(n,19ULL) || u64_r19(n)!=n-u64_dynamic(n,19ULL)*19ULL) return 1;
        if(u64_q37(n)!=u64_dynamic(n,37ULL) || u64_r37(n)!=n-u64_dynamic(n,37ULL)*37ULL) return 1;
        if(u64_q63(n)!=u64_dynamic(n,63ULL) || u64_r63(n)!=n-u64_dynamic(n,63ULL)*63ULL) return 1;
        if(u64_q4294967295(n)!=u64_dynamic(n,4294967295ULL) || u64_r4294967295(n)!=n-u64_dynamic(n,4294967295ULL)*4294967295ULL) return 1;
        if(u64_q4294967297(n)!=u64_dynamic(n,4294967297ULL) || u64_r4294967297(n)!=n-u64_dynamic(n,4294967297ULL)*4294967297ULL) return 1;
        if(u64_q9223372036854775807(n)!=u64_dynamic(n,9223372036854775807ULL) || u64_r9223372036854775807(n)!=n-u64_dynamic(n,9223372036854775807ULL)*9223372036854775807ULL) return 1;
        if(u64_q9223372036854775809(n)!=u64_dynamic(n,9223372036854775809ULL) || u64_r9223372036854775809(n)!=n-u64_dynamic(n,9223372036854775809ULL)*9223372036854775809ULL) return 1;
        if(u64_q18446744073709551615(n)!=u64_dynamic(n,18446744073709551615ULL) || u64_r18446744073709551615(n)!=n-u64_dynamic(n,18446744073709551615ULL)*18446744073709551615ULL) return 1;
        U value=n;
        if(u64_compound(&value)!=u64_dynamic(n,7) || value!=u64_dynamic(n,7)) return 2;
        value=n;
        if(u64_compoundrem(&value)!=n-u64_dynamic(n,10)*10 || value!=n-u64_dynamic(n,10)*10) return 3;
        if(u64_fixed(n,0x13579bdf02468aceULL)!=u64_dynamic(n,7)+n-u64_dynamic(n,10)*10+
            0x13579bdf02468aceULL+(0x13579bdf02468aceULL^0xfedcba9876543210ULL)) return 4;
        if(u64_negative(n)!=u64_dynamic(n,18446744073709551613ULL)) return 5;
        unsigned long narrowed=(unsigned long)n;
        if(oa_div10(narrowed)!=(unsigned long)u64_dynamic(narrowed,10)) return 6;
    }
    if(u64_effect(100)!=10 || effects!=1 || u64_effectrem(100)!=2 || effects!=2) return 7;
    if(u64_rhs(100)!=10 || effects!=3 || u64_rhsrem(100)!=2 || effects!=4) return 8;
    observed=100;
    if(u64_volatile()!=10 || observed!=100 || u64_volatile_compound()!=14 || observed!=14 ||
        u64_volatile_rem()!=4 || observed!=4) return 9;
    U values[]={100,77};
    if(u64_index(values)!=14 || values[0]!=14 || values[1]!=77 || index!=1) return 10;
    U address=(U)(unsigned long)&anchor;
    if(u64_pressure()!=105+15*observed+address-u64_dynamic(address,7)*7) return 11;
    if(u64_signed(-100)!=-14) return 12;
    return 0;
}
