/* [expr.prim.id.qual]/2, [class.mfct.non-static]/3: a base class's member function template named through the base in a member function of the derived class, called on *this: B::f(t), with deduction and with explicit template arguments (the keyword template too) */
struct B {
    template <class T> T f(T t) { return t + base; }
    template <class T> T g(T t) const { return t * 2; }
    int base = 1;
};
struct D : B {
    template <class T> T f(T t) { return B::f(t) + B::template f<T>(t) + 100; }
    int use() { return B::f(1) + B::g<int>(2); }
    int same() { return D::f(1); }
};
int main()
{
    D d;
    return d.use() - 6 + d.same() - 104 + d.f(1) - 104;
}
