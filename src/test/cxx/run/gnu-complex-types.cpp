/* [ext:gnu_complex]: _Complex T and the mode attribute as glibc's
   <bits/floatn.h> and <sys/types.h> use them (typedef _Complex float
   __cfloat128 __attribute__ ((__mode__ (__TC__)));); TF and TC are long
   double on aarch64.  Only the types are analysed: layout, typedefs and
   declarations. */
typedef _Complex float cf;
typedef double _Complex cd;
typedef long double __complex__ cld;
typedef __complex double cd2;
typedef long double f128;
typedef _Complex float tc __attribute__((__mode__(__TC__)));
typedef _Complex float dc __attribute__((__mode__(__DC__)));
typedef float sf __attribute__((mode(SF)));
typedef double tf __attribute__((__mode__(__TF__)));
typedef int i8 __attribute__((__mode__(__QI__)));
typedef unsigned u16 __attribute__((__mode__(__HI__)));
typedef int i32 __attribute__((mode(SI)));
typedef int i64 __attribute__((mode(DI)));
typedef unsigned u128 __attribute__((mode(TI)));
typedef int word __attribute__((__mode__(__word__)));
typedef unsigned char byte_t __attribute__((mode(__byte__)));

static_assert(sizeof(cf) == 8 && alignof(cf) == 4, "");
static_assert(sizeof(cd) == 16 && alignof(cd) == 8, "");
static_assert(sizeof(cd2) == sizeof(cd), "");
static_assert(sizeof(cld) == 2 * sizeof(long double), "");
static_assert(alignof(cld) == alignof(long double), "");
static_assert(sizeof(f128) == 16 && alignof(f128) == 16, "");
static_assert(sizeof(tc) == 32 && alignof(tc) == 16, "");
static_assert(sizeof(dc) == 16, "");
static_assert(sizeof(sf) == 4 && sizeof(tf) == 16, "");
static_assert(sizeof(i8) == 1 && sizeof(u16) == 2 && sizeof(i32) == 4, "");
static_assert(sizeof(i64) == 8 && sizeof(u128) == 16 && sizeof(word) == 8, "");
static_assert(sizeof(byte_t) == 1, "");
static_assert(static_cast<u16>(-1) == 65535, "");
static_assert(static_cast<i8>(200) < 0, "");
static_assert(sizeof(_Complex) == 16, "");
static_assert(sizeof(_Complex long double) == sizeof(cld), "");

struct S {
    char c;
    cd z;
    f128 f;
};
static_assert(sizeof(S) == 48 && alignof(S) == 16, "");

_Complex double global;
extern cf declared;
cf function(cf *p, f128 q);

int main()
{
    return 0;
}
