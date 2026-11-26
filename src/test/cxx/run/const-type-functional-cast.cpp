/* [expr.type.conv] with a const type (Highway's D() where D is the type of
   a const tag): a const class's prvalue stays const, others lose it */
struct S {
    int a = 3;
    int f() const { return 1; }
    int f() { return 2; }
};
static int g(S s) { return s.a; }

template <class D> int h() { return g(D()) + D().f() + D{}.f(); }
template <class T> int zero() { return T() + T(7); }

int main()
{
    using CI = const int;
    return h<const S>() != 5 || h<S>() != 7 || zero<const int>() != 7 ||
           CI(4) != 4;
}
