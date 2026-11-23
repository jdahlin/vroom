// [class.copy.ctor]/7: the implicit copy constructor is X(X&) when a base or member has a copy constructor with no const reference; every value is clang++'s
struct A { A(); A(A&); };
struct B : A {};
struct C { A a; };
static_assert(__is_constructible(B, B&), "");
static_assert(!__is_constructible(B, const B&), "");
static_assert(__is_constructible(C, C&), "");
static_assert(!__is_constructible(C, const C&), "");
static_assert(!__is_constructible(C, C), "");
struct D { C c; };
static_assert(!__is_constructible(D, const D&), "");
static_assert(__is_constructible(D, D&), "");
int main() { return 0; }
