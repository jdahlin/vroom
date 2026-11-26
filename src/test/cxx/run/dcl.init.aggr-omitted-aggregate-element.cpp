/* [dcl.init.aggr]/5.2: an element no clause initializes is copy-initialized
   from an empty list; an aggregate one then initializes its own elements,
   whose constructors and default member initializers run */
static int made;

struct A {
    int n;
    A(int x = 10) : n(x) { made++; }
};
struct B {
    int i;
    A a;
};
struct E {
    int j = 5;
};
struct D {
    int k;
    B b;
    E e;
};

D global = {};
B several[2] = {};

int main()
{
    D local = {};
    if (global.b.a.n != 10 || global.e.j != 5 || global.k || global.b.i)
        return 1;
    if (several[0].a.n != 10 || several[1].a.n != 10)
        return 2;
    if (local.b.a.n != 10 || local.e.j != 5 || local.k || local.b.i)
        return 3;
    if (made != 4)
        return 4;
    return 0;
}
