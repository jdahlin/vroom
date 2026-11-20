/* [class.base.init]/3: a mem-initializer-id may be a simple-template-id
   naming a direct base that is a class template's specialization (the syntax
   tree's views: SyntaxExtra<V>(r)); [class.default.ctor]/1: constructors
   inherited by a using-declaration are no user-declared ones, so the class
   keeps its implicit default constructor ([namespace.udecl]/16: the default
   constructor is not inherited, the class's is) */
struct R {
    int n = 5;
    R() = default;
    R(int n) : n(n) {}
};
template <class V> struct X : R {
    using R::R;
};
struct A : X<A> {
    A() = default;
    explicit A(const R &r) : X<A>(r.n + 1) {}
};
struct Plain : R {
    using R::R;
};
struct Derived : Plain {
    Derived() = default;
};

int main()
{
    Plain p;
    Plain q(7);
    Derived d;
    A a;
    A b(R(2));
    if (p.n != 5 || q.n != 7 || d.n != 5)
        return 1;
    if (a.n != 5)
        return 2;
    return b.n - 3;
}
