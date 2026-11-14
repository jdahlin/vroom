struct E { };
struct E2 { };
struct NP { NP(); long a; char b; };          /* not POD for layout: data size 9 */
struct P { long a; char b; };                 /* POD: 16 */
struct A1 { [[no_unique_address]] E e; int x; };
struct A2 { int x; [[no_unique_address]] E e; };
struct A3 { [[no_unique_address]] E e; [[no_unique_address]] E2 f; };
struct A4 { [[no_unique_address]] NP n; char c; };
struct A5 { [[no_unique_address]] P p; char c; };
struct A6 { NP n; char c; };
struct A7 { [[no_unique_address]] int i; char c; };
struct A8 { struct { [[no_unique_address]] long *p; [[no_unique_address]] E a; [[no_unique_address]] E2 pad; }; };
struct A9 { char c; struct __attribute__((packed)) { unsigned char a : 1; unsigned char b : 7; }; char d[23]; };
struct __attribute__((packed)) B1 { char c; int i; };
struct B2 { char c; B1 b; };
struct F final { };
struct C1 : E { [[no_unique_address]] E2 e; long v; };
template <class T, class A> struct V { T *b, *e; struct { [[no_unique_address]] T *cap; [[no_unique_address]] A alloc; }; };
#define L(T, s, a) static_assert(sizeof(T) == s && alignof(T) == a, #T)
L(A1, 4, 4); L(A2, 4, 4); L(A3, 1, 1); L(A4, 16, 8); L(A5, 24, 8); L(A6, 24, 8); L(A7, 8, 4); L(A8, 8, 8); L(A9, 25, 1);
L(B1, 5, 1); L(B2, 6, 1); L(C1, 8, 8);
static_assert(sizeof(V<int, E>) == 24 && sizeof(V<int, NP>) == 40, "V");
static_assert(__datasizeof(NP) == 9 && __datasizeof(P) == 16 && __datasizeof(E) == 1 && __datasizeof(int) == 4 && __datasizeof(A4) == 10, "ds");
static_assert(__is_final(F) && !__is_final(E) && !__is_final(int), "final");
static_assert(__is_empty(A3) && !__is_empty(A1) && __is_empty(E), "empty");
static_assert(sizeof(A9) == 25, "s"); static_assert(alignof(A9) == 1, "a"); struct A10 { struct __attribute__((packed)) { unsigned char a : 1; unsigned char b : 7; }; }; static_assert(sizeof(A10) == 1, "A10");
