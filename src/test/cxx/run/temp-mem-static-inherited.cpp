/* [temp.mem]/1, [class.member.lookup]: a static member function template of a base class called through derived classes, objects and pointers of them, and unqualified in derived class members */
struct B {
    template <class T> static T f(T t) { return t + 1; }
};
struct D : B {
    int g() { return f(1) + f<int>(1) + B::f(1); }
};
struct E : D {};
int main()
{
    D d;
    E e;
    E *p = &e;
    return d.f(1) + D::f(1) + e.f<int>(1) + p->f(1) + E::f(1) + d.g() + e.g() - (2 * 5 + 6 * 2);
}
