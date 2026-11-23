/* [dcl.fct.def.default], [dcl.fct.def.delete]: defaulted and deleted
   special members */
struct A {
    int x;
    A() = default;
    A(int v) : x(v) {}
    A(const A &) = default;
    ~A() = default;
};
struct B {
    int y;
    B(const B &) = delete;
    B(int v) : y(v) {}
};
int main()
{
    A a(3);
    A b(a);
    A c;
    c.x = 4;
    B d(5);
    return a.x + b.x + c.x + d.y == 15 ? 0 : 1;
}
