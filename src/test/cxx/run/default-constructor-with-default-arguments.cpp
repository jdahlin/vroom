/* [class.default.ctor]/1: a base or member whose default constructor has
   only parameters with default arguments does not delete the class's
   defaulted or implicit default constructor ([class.default.ctor]/2) */
struct S {
    int v = 3;
    S() = default;
    S(int x) : v(x) {}
};
struct B {
    S str{};
    int n;
    B(const S &s = {}, int k = 4) : n(s.v + k) {}
};
struct D : public B {
    D() = default;
    explicit D(const S &) {}
};
struct M {
    B member;
};
int main()
{
    D d;
    M m;
    D list[2];
    if (d.str.v != 3 || d.n != 7)
        return 1;
    if (m.member.n != 7)
        return 2;
    if (list[1].n != 7)
        return 3;
    return 0;
}
