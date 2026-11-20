/* [expr.ref]/4, [class.mfct.non-static]/3: E1.B::f(args) and E1->B::f<T>(args), a qualified name after . and -> naming a member function template of a base class (this->B::f(t), d.B::f(1)) */
struct B {
    template <class T> T f(T t) { return t + base; }
    template <class T> T g(T t) const { return t * 2; }
    int base = 1;
};
struct D : B {
    template <class T> T f(T t) { return t + 100; }
    int use() { return this->B::f(3) + this->B::template f<int>(1); }
};
int main()
{
    D d;
    D *p = &d;
    return d.B::f(1) - 2 + p->B::f<int>(2) - 3 + d.use() - 6;
}
