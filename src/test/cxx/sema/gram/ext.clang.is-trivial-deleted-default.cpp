// [ext:clang_type_traits] __is_trivial: clang counts X() = delete as a
// trivial default constructor ([dcl.fct.def.default]/5: not user-provided),
// also beside other constructors; a class without any default constructor,
// or with a user-provided, defaulted-with-initializer or virtual one is not
// trivial.
struct A1 { A1() = delete; };
struct A2 { A2() = default; A2(int) {} };
struct A3 { A3() = delete; A3(int) {} };
struct A4 { A4() {} };
struct A5 { A5() = delete; A5(const A5 &) = default; };
struct A6 { A6(int); };
struct A7 { A7() = delete; int i; };
struct A8 { A8() = delete; int i = 0; };
struct A9 { A9() = delete; virtual void f(); };
struct A14 { A14() = delete; A14(int) = delete; };
struct A15 { A15() = delete; A15(int i = 0) {} };
struct B { B() = delete; };
struct A20 { B b; };
static_assert(__is_trivial(A1));
static_assert(__is_trivial(A2));
static_assert(__is_trivial(A3));
static_assert(!__is_trivial(A4));
static_assert(__is_trivial(A5));
static_assert(!__is_trivial(A6));
static_assert(__is_trivial(A7));
static_assert(!__is_trivial(A8));
static_assert(!__is_trivial(A9));
static_assert(__is_trivial(A14));
static_assert(!__is_trivial(A15));
static_assert(__is_trivial(A20));
