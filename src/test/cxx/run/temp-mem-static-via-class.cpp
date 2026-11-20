/* [expr.prim.id.qual]/2, [class.mfct.non-static]/3: a static member function template called through the class name, unqualified in a member function, in a static member function, in a lambda, and through a derived class and an object of it */
struct B {
    template <class T> static T twice(T t) { return t + t; }
};
struct S : B {
    template <class T> static T f(T t) { return t + 1; }
    template <class T> T inst(T t) { return f(t) + f<T>(t) + twice(t); }
    template <class T> static T stat(T t) { return f(t) + f<T>(t) + B::twice(t); }
    static int lam(int x)
    {
        auto l = [](int y) { return f(y) + S::f(y) + twice(y); };
        return l(x);
    }
    int lam_this(int x)
    {
        auto l = [this](int y) { return f(y) + twice<int>(y); };
        return l(x);
    }
};
int main()
{
    S s;
    int r = S::f(1) + S::f<int>(1) + S::twice(2) + S::template twice<int>(2) + B::twice(3) + s.twice(4);
    r += s.inst(1) + S::stat(1) + S::lam(1) + s.lam_this(1);
    return r - (2 + 2 + 4 + 4 + 6 + 8 + (2 + 2 + 2) + (2 + 2 + 2) + (2 + 2 + 2) + (2 + 2));
}
