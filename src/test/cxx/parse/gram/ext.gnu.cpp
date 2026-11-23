// RUN: %clang_cc1 -std=gnu++17 %s
// [ext.gnu] GCC's extensions that clang accepts (doc/PLAN-cxx-parse.md P9;
// src/frontend/cplusplus/ext/gnu/): attributes (GCC manual 6.39), asm
// labels (6.47.4), __restrict__ (7.3), __extension__ and the alternate
// keywords (6.48).
__inline int f1();
__inline__ int f2();
__signed__ char c1;
int __const__ k1 = 1;
__volatile__ int v1;
static_assert(__alignof__(int) == alignof(int));

// attributes: before and among the decl-specifiers, after a declarator,
// after parameters, after a namespace name, on a class
__attribute__((__visibility__("default"))) int a1;
int __attribute__((unused)) a2, a3 __attribute__((unused)) = 3;
static __attribute__((noinline)) int a4();
typedef int v4si __attribute__((__vector_size__(16)));
typedef int __attribute__((aligned(16))) aligned_int;
static_assert(alignof(aligned_int) == 16);
void a5(int p __attribute__((unused)), int __attribute__((unused)) q);
void a6() __attribute__((cold));
void a7() __attribute__((noinline)) {}
struct __attribute__((packed)) S1 { char c; int i; };
static_assert(sizeof(S1) == 5);
struct S2 { char c; int i; } __attribute__((packed));
static_assert(sizeof(S2) == 5);
struct S3 { int m __attribute__((aligned(8))); void h() __attribute__((cold)) {} };
namespace ns __attribute__((__visibility__("default"))) { int in; }
__attribute__((__visibility__("default"))) [[nodiscard]] int a8();
[[nodiscard]] __attribute__((__visibility__("default"))) int a9();
enum E1 { e1 __attribute__((deprecated)), e2 };
int a10 __attribute__((__format__(__printf__, 1, 2), __nonnull__(1)));
using U1 __attribute__((aligned(8))) = int;
static_assert(alignof(U1) == 8);

// asm labels, then attributes
int l1 asm("l1_name");
int l2 __asm__("l2_" "name") __attribute__((unused));
void l3() __asm("l3_name");
extern "C" int l4(int) __asm__("l4_name") __attribute__((__nothrow__));

// __restrict__ is a cv-qualifier
int r1(char *__restrict p, const char *__restrict__ q);
struct R { void g() __restrict; };
int *__restrict r2;

// __extension__ before a declaration, a member, a block declaration and
// an expression
__extension__ typedef long long ll;
struct X1 { __extension__ unsigned long long u; };
static_assert(__extension__ 1 + 1 == 2);
int x2() {
  __extension__ int a = __extension__ 2;
  __extension__ a = 3;
  __attribute__((unused)) int b = a;
  return a;
}

// complex numbers (6.12): _Complex, __complex__ and __complex are a modifier
// of float, double, long double and __float128 in any order; alone it is
// _Complex double; the mode attribute (6.39) names the size of a type
typedef _Complex float gc1;
typedef double _Complex gc2;
typedef long double __complex__ gc3;
typedef __complex double gc4;
typedef _Complex __float128 gc5;
typedef __float128 gc6;
typedef _Complex float gc7 __attribute__ ((__mode__ (__TC__)));
typedef int gc8 __attribute__ ((__mode__ (__word__)));
typedef unsigned gc9 __attribute__ ((mode(QI)));
static_assert(sizeof(gc1) == 8 && alignof(gc1) == 4);
static_assert(sizeof(gc2) == 16 && sizeof(gc3) == 32 && sizeof(gc4) == 16);
static_assert(sizeof(gc5) == 32 && alignof(gc5) == 16 && sizeof(gc6) == 16);
static_assert(sizeof(gc7) == 32 && sizeof(gc8) == 8 && sizeof(gc9) == 1);
static_assert(sizeof(_Complex) == 16);
_Complex double gc10;
extern gc1 gc11;
gc1 gc12(gc1 *p, gc6 q);
