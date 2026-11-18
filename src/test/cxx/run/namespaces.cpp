// [namespace.def]: members of named, nested, inline and unnamed namespaces,
// a namespace extended by a later definition; [basic.lookup.unqual]: names
// of the enclosing namespaces from a namespace's function, an inline and an
// unnamed namespace's members from the enclosing one ([namespace.def]/8,
// [namespace.unnamed]/1); [basic.link]/4: internal linkage in an unnamed
// namespace; [abi:mangle.name] <nested-name>, St for ::std
namespace A {
int v = 3;
int f(int x) { return x + v; }
namespace B {
struct C {
    int m;
    int get() { return m + v; }
};
int w = 4;
int g() { return f(w) + v; }
} // namespace B
inline namespace I {
int i1 = 5;
}
int h() { return i1; }
} // namespace A
namespace {
int u = 7;
int k() { return u; }
} // namespace
namespace A {
int again() { return v + h(); }
} // namespace A
namespace std {
int sf(int a) { return a; }
struct S {
    int x;
};
int ss(S *p) { return p->x; }
namespace n2 {
int t() { return sf(1); }
} // namespace n2
} // namespace std
namespace X::Y {
int z = 1;
int zz(int *p, int *q) { return *p + *q + z; }
} // namespace X::Y
namespace A {
namespace B {
int cc(C *a, C *b, C c) { return a->m + b->m + c.get(); }
} // namespace B
} // namespace A
static int st() { return 1; }
namespace A {
static int st2() { return 2; }
const int kc = 9;
int usekc() { return kc + st2(); }
int shadow = 1;
namespace B {
int shadow = 2;
int inner() { return shadow; }
} // namespace B
int outer() { return shadow; }
} // namespace A
namespace X {
namespace Y {
int more() { return z + 1; }
} // namespace Y
int twice() { return 2; }
} // namespace X
int main()
{
    if (u + k() + st() != 15)
        return 1;
    return 0;
}
