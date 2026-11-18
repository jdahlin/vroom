// [dcl.meaning], [dcl.ptr], [dcl.ref], [dcl.mptr], [dcl.array], [dcl.fct],
// [dcl.type.simple] Table 14: the type each declarator gives its name, which
// clang checks against -fcxx-dump=declarator-claims (bench/cxx-parse.py).
struct S {
  int m;
  int f(char) const;
};
enum E { e0 };
enum class C : unsigned char { c0 };
namespace N {
struct T {};
typedef long L;
}

// how *, &, [] and () nest
int *p1, **p2, *const p3 = 0, *const *volatile p4 = 0;
int a1[2], a2[2][3], *a3[4], (*a4)[4], *(*a5[2])[3], (*(*a6)[2])[3];
int &r1 = a1[0], &&r2 = 1, (&r3)[2] = a1, *&r4 = p1, *const &r5 = p3;
int f1(), *f2(), (*f3)(), *(*f4)(), (*f5())(), (*(*f6)())[2], (*f7[3])(int);
int (*(*f8(char))(long))[5];
void (*f9(int, void (*)(int)))(int);
int (*const (*f10)[2])(double) = 0;
const int *q1, *const q2 = 0, *const volatile q3 = 0;
int const volatile *volatile const q4 = 0;
int S::*m1, S::*const m2 = 0, *S::*m3, S::**m4, (S::*m5)(char) const, (S::*m6[2])(char) const;
int (S::*const (*m7)[3])(char) const = 0;
int (*g1(int (*)(int), int (&)[2]))(int (*)[3]);

// the parameters' own types: adjusted by [dcl.fct]/5 on both sides
void h1(int[3], int[], int(), const int, int *const, void (int)), h2(void), h3(...), h4(int, ...);
void h5(int a[], int (*b)(char), char c = 0, S = S(), N::T = N::T{});
void h6() noexcept, h7() noexcept(true), h8() noexcept(false), h9() noexcept(sizeof(int) == 4);
void (*h10)() noexcept = 0, (*h11(void (*)() noexcept))();

// Table 14: the specifiers in any order
unsigned u1; signed u2; long u3; short u4; long long u5; unsigned long u6;
int unsigned u7; long unsigned int long u8; short signed int u9; long int signed long u10;
char signed u11; unsigned char u12; char u13; long double u14; double long u15;
int long unsigned u16; short unsigned u17; long signed u18; const long volatile unsigned u19 = 0;
wchar_t w1; char8_t w2; char16_t w3; char32_t w4; bool w5; float w6; double w7;
unsigned const u20 = 0; int static long u21; long inline constexpr unsigned u22 = 0;

// named types, elaborated and qualified, and what a declaration defines
S s1, *s2, s3[2]; struct S s4; const struct S *s5; E e1; enum E e2; C c1 = C::c0;
N::T n1; ::N::T *n2; N::L n3; const ::N::L n4 = 0; struct N::T n5;
struct D1 { int x; } d1, *d2, d3[2];
enum D2 { d2a } d4, *d5;
enum class D3 : short { d3a } d6[3];
decltype(u6) t1; decltype((u6)) t2 = u6; decltype(a2) t3; const decltype(p1) t4 = 0;

// typedef-names and alias-declarations
typedef int I, *IP, IA[3], IF(char), (*IPF)(char), &IR, (&IRA)[2];
typedef const I CI; typedef CI *CIP; typedef IA IAA[2]; typedef IF *IFP;
typedef int (S::*SM)(char) const; typedef unsigned long int UL;
using X1 = int; using X2 = int *; using X3 = int[3]; using X4 = int (*)(char);
using X5 = int (&)[2]; using X6 = int S::*; using X7 = int (S::*)(char) const;
using X8 = const unsigned long long int *const volatile; using X9 = void(int, ...);
using X10 = void() const; using X11 = void() &; using X12 = void() const && noexcept;
using X13 = auto() -> int; using X14 = auto(int) -> long (*)[2]; using X15 = auto (*)(char) -> int *;
I i1; IP i2; IA i3; IF i4; IPF i5; CI i6 = 0; CIP i7; IAA i8; IFP i9; SM i10; UL i11;
const IA i12 = {}; IA *i13; I *const i14[2] = {};

// a trailing-return-type, constexpr, an array of unknown bound
auto k1() -> int;
auto k2(int) -> int (*)[3];
auto k3() -> auto (*)(char) -> long;
auto (*k4)() -> int = 0;
auto (*k5())() -> S *;
constexpr int v1 = 1, *v2 = 0, v3[2] = {1, 2};
constexpr const int &v4 = v1;
constexpr int k6() { return 0; }
constexpr const int *v5 = &v1;
extern int o1[], o2[][3];
int o3[] = {1, 2, 3}, o4[][2] = {{1, 2}, {3, 4}}, *o5[] = {0};
char o6[] = "abc";
typedef int O7[];
extern "C" int l1(int), (*l2)(char);
extern "C" {
int l3[2];
void l4(int (*)(void));
}
namespace N {
int z1, *z2[2];
T (*z3(T &))[2];
}
int def1(int a, int *b) { return a + *b; }
int (*def2(char c))[2] { return 0; }
static inline unsigned long def3(void) { return 0; }
