/* [temp.mem]/2, [namespace.udecl]: a using-declaration of a base class's member function template (and a non-template one of the same name) brought into a derived class; overload resolution among the derived class's own and the base's */
struct B {
    template <class T> int f(T) { return 1; }
    int f(int) { return 2; }
};
struct D : B {
    using B::f;
    template <class T> int f(T *) { return 3; }
};
int main()
{
    D d;
    int x = 0;
    return d.f('a') - 1 + d.f(1) - 2 + d.f(&x) - 3;
}
