// [namespace.udecl]/1: a using-declaration of a class template, an alias
// template and a variable template makes the name a synonym for it in the
// namespace that declares it (Skia's `using
// partition_alloc::internal::raw_ptr;`).
namespace a::b {
template <class T, int Tr = 0> class R { public: constexpr R(T *p) : q(p) {} T *get() const { return q; } T *q; };
template <class T> using A = R<T, 1>;
template <class T> constexpr int V = sizeof(T);
}
using a::b::R;
using a::b::A;
using a::b::V;
namespace c { using a::b::R; }
template <class T, int Tr> T *Ref(const R<T, Tr> &o) { return o.get(); }
int x = 3;
static_assert(V<int> == 4);
int main()
{
    R<int> r(&x);
    c::R<int, 2> r2(&x);
    A<int> r3(&x);
    if (*Ref(r) + *Ref(r2) + *Ref(r3) != 9)
        return 1;
    return 0;
}
