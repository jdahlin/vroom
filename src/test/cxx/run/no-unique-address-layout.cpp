/* [dcl.attr.nouniqueaddr]/2: [[no_unique_address]] on a non-static data
   member makes it potentially-overlapping ([intro.object]/7).  The layout
   is [abi:class-types] II as clang does it: an empty class's member takes no
   bytes and starts at offset 0, moving when an empty subobject of its type
   is there; another class's member takes its data size, so a later member
   uses its tail padding unless it is a POD for the purpose of layout.
   Every value below was computed by clang++.  An array's elements are laid
   out as any member's (A9).  Limit: the lowering's layout is C's, so a
   class laid out otherwise is refused there ("a class whose layout is not
   C's", xfail.txt): this file checks the analysis' layout by its
   static_asserts alone, and the attribute after the declarator is not
   analysed yet. */
struct E {};
struct F {};
struct A1 {
    int i;
    [[no_unique_address]] E e;
};
struct A2 {
    char c;
    [[no_unique_address]] E e;
};
struct A3 {
    [[no_unique_address]] E e;
    [[no_unique_address]] E f;
};
struct A4 {
    [[no_unique_address]] E e;
    [[no_unique_address]] F f;
    int i;
};
struct A5 {
    [[no_unique_address]] E e;
    int i;
    [[no_unique_address]] E f;
};
struct A6 {
    [[no_unique_address]] E e;
};
struct B6 : A6 {
    int i;
};
struct P {
    int i;
    char c;
    P() {}
};
struct A7 {
    [[no_unique_address]] P p;
    char d;
};
struct Q {
    int i;
    char c;
};
struct A8 {
    [[no_unique_address]] Q q;
    char d;
};
struct A9 {
    [[no_unique_address]] E e[2];
    int i;
};
struct A10 {
    char c;
    [[no_unique_address]] E e;
    int i;
};
struct B4 {
    int i;
    int j;
    [[no_unique_address]] E e;
    [[no_unique_address]] E f;
    [[no_unique_address]] E g;
};
struct B5 {
    [[no_unique_address]] E e;
    [[no_unique_address]] E f;
    [[no_unique_address]] E g;
    int i;
};
template <class T> struct S {
    [[no_unique_address]] T v;
    char c;
};
struct D {
    [[no_unique_address]] int x;
    [[no_unique_address]] char c;
};

static_assert(sizeof(A1) == 4 && __builtin_offsetof(A1, i) == 0);
static_assert(sizeof(A2) == 1 && __builtin_offsetof(A2, e) == 0);
static_assert(sizeof(A3) == 2 && __builtin_offsetof(A3, f) == 1);
static_assert(sizeof(A4) == 4 && __builtin_offsetof(A4, f) == 0 &&
              __builtin_offsetof(A4, i) == 0);
static_assert(sizeof(A5) == 8 && __builtin_offsetof(A5, f) == 4);
static_assert(sizeof(A6) == 1);
static_assert(sizeof(B6) == 4 && alignof(B6) == 4);
static_assert(sizeof(A7) == 8 && __builtin_offsetof(A7, d) == 5);
static_assert(sizeof(A8) == 12 && __builtin_offsetof(A8, d) == 8);
static_assert(sizeof(A9) == 8);
static_assert(sizeof(A10) == 8 && __builtin_offsetof(A10, e) == 0 &&
              __builtin_offsetof(A10, i) == 4);
static_assert(sizeof(B4) == 12 && __builtin_offsetof(B4, f) == 8 &&
              __builtin_offsetof(B4, g) == 9);
static_assert(sizeof(B5) == 4 && __builtin_offsetof(B5, f) == 1 &&
              __builtin_offsetof(B5, g) == 2);
static_assert(sizeof(S<E>) == 1 && sizeof(S<int>) == 8);
static_assert(sizeof(D) == 8);

int main()
{
    return 0;
}
