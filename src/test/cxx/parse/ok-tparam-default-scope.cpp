// typename Attrs = Attrs<...>: the default names the enclosing Attrs, the parameter
// is in scope only after it (Skia's GrUserStencilSettings); later defaults see it
struct S {
    template <int T> struct Attrs { static constexpr int v = T; };
    template <int R, typename Attrs = Attrs<R>> static constexpr int f() { return Attrs::v; }
};
int g() { return S::f<3>(); }
template <class T, class U = T> struct P { U u; };
static_assert(sizeof(P<char>) == 1);
