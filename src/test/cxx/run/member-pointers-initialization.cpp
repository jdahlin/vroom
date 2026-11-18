/* [dcl.mptr], [dcl.init]/6, [conv.mem]/1: pointers to members as
   variables, members, array elements and static locals, zero-initialized
   (a null pointer to data member is -1, [abi:data-member-pointers]),
   value-initialized and list-initialized; chosen by overload resolution,
   in a conditional expression, returned and initialized by a constructor */
struct A {
    int x;
    int y;
    int f() { return 5; }
    virtual int g() { return 6; }
};
int A::*zeroed;
struct S {
    int A::*p;
    int (A::*q)();
};
S aggregate = {&A::y, &A::f};
int A::*table[2] = {&A::x, &A::y};
int nulls()
{
    static int A::*local;
    int A::*n{};
    return (local == 0) + (n == nullptr) + (zeroed == 0);
}
int read(const A &a, int A::*p) { return a.*p; }
int call(A *a, int (A::*p)()) { return (a->*p)(); }
int pick(int A::*) { return 1; }
int pick(int) { return 2; }
int (A::*returned())() { return &A::f; }
struct Held {
    int A::*p;
    int (A::*q)();
    Held() : p(&A::y), q(&A::f) {}
};
int main()
{
    A a;
    a.x = 1;
    a.y = 2;
    if (nulls() != 3)
        return 1;
    if (read(a, aggregate.p) != 2 || read(a, table[0]) != 1)
        return 2;
    if (call(&a, aggregate.q) != 5 || call(&a, &A::g) != 6)
        return 3;
    int A::*q = (int A::*)0;
    if (q)
        return 4;
    const A &ca = a;
    const int &r = ca.*table[1];
    if (r != 2)
        return 5;
    A *pa = &a;
    (pa->*(table[1])) = 9;
    if (a.y != 9)
        return 6;
    if (pick(&A::x) != 1 || pick(0) != 2)
        return 7;
    if ((a.*returned())() != 5)
        return 8;
    Held h;
    if (a.*h.p != 9 || (a.*h.q)() != 5)
        return 9;
    bool b = &A::x;
    int A::*c = b ? &A::x : &A::y;
    if (a.*c != 1)
        return 10;
    return 0;
}
