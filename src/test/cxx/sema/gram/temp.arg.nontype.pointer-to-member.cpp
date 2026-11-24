// [temp.arg.nontype]/2: a pointer to member template parameter's argument is &C::m or nullptr
struct S { int m; void f(); };
template <int S::*P> struct A {};
template <void (S::*P)()> struct B {};
A<&S::m> a;
A<nullptr> b;
B<&S::f> c;
B<nullptr> d;
