/* [dcl.fct.default]/4, [temp.deduct.call]/1: default arguments of member
   function templates, static and not, left out of a call; of a member
   template of a class template; the default argument names another member
   ([dcl.fct.default]/9 allows a static one). */
struct S {
    static int base() { return 10; }
    template <class T> static T s(T t, int k = base()) { return t + T(k); }
    template <class T = long> T f(T t = T(3), T u = T(4)) const
    {
        return t * u;
    }
};
template <class U> struct C {
    template <class T> U g(T t, U u = U(2)) { return U(t) + u; }
};
int main()
{
    S o;
    C<int> c;
    int r = S::s(1) + o.s(2, 1) + int(o.f()) + o.f(2) + o.f<int>(1, 1) +
            c.g('a') + c.g(1, 1);
    return r - (11 + 3 + 12 + 8 + 1 + 99 + 2);
}
