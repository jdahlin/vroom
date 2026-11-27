// [class.copy.assign]/3: the implicit move assignment where a member's copy is
// a template or deleted (std::unique_ptr).
struct U {
    int n = 0;
    U() = default;
    U(U &&o) : n(o.n) {}
    U &operator=(U &&o) { n = o.n + 1; return *this; }
    template <class E> U &operator=(E &&) { n = -100; return *this; }
};
struct C { U a; U b; };
int main()
{
    C c, d;
    d.a.n = 4;
    c = static_cast<C &&>(d);
    if (c.a.n != 5 || c.b.n != 1)
        return 1;
    c = {};
    return !(c.a.n == 1 && c.b.n == 1);
}
