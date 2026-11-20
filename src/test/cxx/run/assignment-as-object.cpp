// [expr.ass]/1 and [expr.pre.incr]/1: an assignment, a compound assignment
// and a prefix increment or decrement yield their left operand, an lvalue; it
// is used as an object (member access, reference binding, address, member
// call, chained class assignment), the left operand evaluated once.
// [expr.ref]/6.3: a member of a class prvalue is an xvalue, a temporary the
// object expression is materialized into ([class.temporary]/2).
struct P {
    int x, y;
    int sum() const { return x + y; }
    void bump() { x++; }
};
struct C {
    int v;
    C() : v(0) {}
    C(int n) : v(n) {}
    C &operator=(const C &o)
    {
        v = o.v + 100;
        return *this;
    }
};
struct B {
    int c;
    bool is(int k) const { return c == k; }
};
struct E : B {
    E(int v) { c = v; }
};
struct Plain {
    int a, b;
};
// non-POD for the layout ([abi:pod]): a derived class's member may sit in
// its tail padding, so an assignment copies the data size only
struct Tail {
    long x;
    char c;
    Tail() : x(0), c(0) {}
    Tail(long a, char b) : x(a), c(b) {}
};
struct Derived : Tail {
    char d;
};
static Tail make_tail(long a, char b)
{
    return Tail(a, b);
}
static int calls;
static P *pick(P *p)
{
    calls++;
    return p;
}
static E make(int v)
{
    return E(v);
}
static Plain plain(int a, int b)
{
    Plain p = {a, b};
    return p;
}
static int &bound(int &r)
{
    return r;
}
int main()
{
    P a = {1, 2}, b = {3, 4};
    P arr[2] = {{0, 0}, {0, 0}};
    P *p = arr;
    // member of an assignment, the pointer incremented once
    int x = (*p++ = a).x;
    if (x != 1 || p != arr + 1 || arr[0].y != 2)
        return 1;
    // a member call on an assignment's result
    if ((a = b).sum() != 7 || a.x != 3)
        return 2;
    (a = P{5, 6}).bump();
    if (a.x != 6 || a.y != 6)
        return 3;
    // reference binding
    P &r = (a = b);
    r.y = 9;
    if (a.y != 9 || &r != &a)
        return 4;
    // address of an assignment, a compound assignment, a prefix increment
    int i = 1, j = 5;
    int *q = &(i = j);
    if (q != &i || i != 5)
        return 5;
    int *q2 = &(i += 2);
    if (q2 != &i || i != 7)
        return 6;
    int *q3 = &++i;
    if (q3 != &i || i != 8)
        return 7;
    int *q4 = &--i;
    if (q4 != &i || i != 7)
        return 8;
    ++++i;
    if (i != 9)
        return 9;
    (i = 3) = 4;
    if (i != 4)
        return 10;
    (i += 1) *= 3;
    if (i != 15)
        return 11;
    bound(i = 2) = 6;
    if (i != 6)
        return 12;
    // the left operand with side effects, evaluated once
    calls = 0;
    P *base = arr;
    (*pick(base) = b).y = 8;
    if (calls != 1 || arr[0].x != 3 || arr[0].y != 8)
        return 13;
    calls = 0;
    int &z = (pick(base)->x += 4);
    z++;
    if (calls != 1 || arr[0].x != 8)
        return 14;
    // chained assignment of a class with an assignment operator
    C c1, c2, c3(7);
    c1 = c2 = c3;
    if (c2.v != 107 || c1.v != 207)
        return 15;
    (c1 = c3).v = 1;
    if (c1.v != 1)
        return 16;
    // chained built-in class assignment
    Plain u = {1, 2}, v = {0, 0}, w = {0, 0};
    u = v = w = Plain{7, 8};
    if (u.a != 7 || v.b != 8 || w.a != 7)
        return 17;
    Plain *pu = &(u = plain(1, 1));
    if (pu != &u || u.a != 1)
        return 18;
    // a member of a prvalue, as an object
    if (!make(3).is(3) || make(3).is(4))
        return 19;
    if (plain(4, 5).b != 5)
        return 20;
    const int &cr = plain(6, 7).a;
    if (cr != 6)
        return 21;
    // a class with reusable tail padding assigned from a prvalue
    Derived dt;
    dt.d = 'q';
    static_cast<Tail &>(dt) = make_tail(11, 'z');
    if (dt.x != 11 || dt.c != 'z' || dt.d != 'q')
        return 22;
    Tail t1, t2;
    (t1 = make_tail(5, 'a')).c = 'b';
    t2 = t1 = make_tail(6, 'c');
    if (t1.x != 6 || t2.c != 'c')
        return 23;
    return 0;
}
