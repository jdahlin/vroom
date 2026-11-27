/* [ext:gnu_offsetof] a member found in a base class. */
extern "C" int printf(const char *, ...);
struct A { int a; short s[3]; };
struct B { char b; union { int u; char v; }; };
struct C : A, B { int c; };
struct D : C { A inner; };
struct E : D { short a; };
int main()
{
    printf("%d %d %d %d\n", static_cast<int>(__builtin_offsetof(C, a)),
           static_cast<int>(__builtin_offsetof(C, b)),
           static_cast<int>(__builtin_offsetof(C, v)),
           static_cast<int>(__builtin_offsetof(C, s[2])));
    printf("%d %d %d\n", static_cast<int>(__builtin_offsetof(D, c)),
           static_cast<int>(__builtin_offsetof(D, inner.s[1])),
           static_cast<int>(__builtin_offsetof(E, a)));
}
