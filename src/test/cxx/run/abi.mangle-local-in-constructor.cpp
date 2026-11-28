/* [abi:mangling-scope] an entity local to a constructor or destructor is
   named in the complete object's (C1, D1), as clang names it: a closure as
   a template argument, a static local and its guard, a local class */
struct A {
    int v = 0;
    template <class F> void g(F f) { v += f(); }
    A(int x)
    {
        static int s = x;
        struct L {
            int get() { return 1; }
        };
        g([x] { return x; });
        v += s + L{}.get();
    }
    ~A()
    {
        static int d = 0;
        g([] { return ++d; });
    }
};
struct B : A {
    B(int x) : A([x] { return x + 1; }()) { g([x] { return x; }); }
};

int main()
{
    B b(2);
    return b.v - 11;
}
