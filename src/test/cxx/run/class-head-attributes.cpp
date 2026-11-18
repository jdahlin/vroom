/* [dcl.attr.deprecated] and GNU's deprecated in a class-head: nothing
   generated (libc++'s _LIBCPP_DEPRECATED_IN_CXX20 is_pod) */
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
struct [[deprecated]] A { int a; };
struct __attribute__((deprecated)) B { int b; };
struct __attribute__((__deprecated__("old"))) C { int c; };
template <class T> struct [[deprecated("use U")]] D { T d; };
template <class T> struct __attribute__((deprecated)) E : D<T> {};

int main()
{
    A a = {1};
    B b = {2};
    C c = {3};
    E<long> e;
    e.d = 4;
    return a.a + b.b + c.c + (int)e.d == 10 ? 0 : 1;
}
