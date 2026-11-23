// [class.virtual], [class.abstract], [class.cdtor]/4: overriding, calls
// through base pointers and references, qualified calls, calls during
// construction, pure virtual functions, the vtable symbols of classes
// with and without a key function ([abi:vague-vtable])
extern "C" int printf(const char *, ...);
struct A {
    A() : x(1) { printf("A() sees %d\n", who()); }
    virtual int who() const { return 1; }
    virtual int area() const = 0;
    int twice() const { return 2 * area(); }
    int x;
};
struct B : A {
    B(int s) : side(s) { printf("B() sees %d\n", who()); }
    int who() const override { return 2; }
    int area() const override { return side * side; }
    int base_who() const { return A::who(); }
    int side;
};
struct C : B {
    C() : B(3) {}
    int area() const final { return 100 + A::x; }
};
// key functions: K's vtable is emitted here, strongly
struct K {
    K() : v(5) {}
    virtual int k() const;
    virtual int j() const { return 7; }
    int v;
};
int K::k() const { return v; }
struct L : K {
    int k() const override;
    int j() const override { return K::j() + 1; }
};
int L::k() const { return 9; }
int via(const K *p) { return p->k() * 10 + p->j(); }
// a dynamic class whose base is not: the vptr first, the base after it
struct N {
    int n;
};
struct M : N {
    M() { n = 11; }
    virtual int m() const { return n; }
};
struct O : M {
    int m() const override { return n + 1; }
};
int call(const A &a) { return a.area() + a.who(); }
int main() {
    B b(4);
    C c;
    A *p = &c;
    printf("%d %d %d %d\n", call(b), call(c), p->twice(), b.base_who());
    printf("%d %d %d\n", (int)sizeof(A), (int)sizeof(B), (int)sizeof(C));
    K k;
    L l;
    printf("%d %d\n", via(&k), via(&l));
    O o;
    M &mr = o;
    printf("%d %d %d\n", mr.m(), (int)sizeof(M), (int)sizeof(O));
    B copy(b);
    printf("%d\n", call(copy));
    return 0;
}
