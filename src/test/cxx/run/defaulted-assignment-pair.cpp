// [class.copy.assign]/2, /3: a class may default its copy and its move
// assignment operator, and a base class's operator= is not a declaration of
// the derived class ([class.member.lookup]).
struct L {
    int v;
    L() : v(0) {}
    L &operator=(const L &o) noexcept
    {
        v = o.v + 1;
        return *this;
    }
};
// declared defaulted beside a base class's user-provided one
struct D : L {
    D &operator=(const D &) noexcept = default;
};
struct F {
    int a;
    F() : a(0) {}
    F &operator=(const F &) noexcept = default;
    F &operator=(F &&) noexcept = default;
};
int main()
{
    F f, g;
    g.a = 3;
    f = g;
    if (f.a != 3)
        return 1;
    g.a = 4;
    f = static_cast<F &&>(g);
    if (f.a != 4)
        return 2;
    return 0;
}
