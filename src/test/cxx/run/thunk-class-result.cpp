/* [abi:calls], [class.virtual]: a thunk of a function returning a class by
   address builds the result in its caller's object */
extern "C" int printf(const char *, ...);
struct self {
    self *me;
    long v;
    self(long x) : me(this), v(x) { printf("make %ld\n", x); }
    self(const self &o) : me(this), v(o.v) { printf("copy %ld\n", v); }
    ~self() { printf("drop %ld %d\n", v, me == this); }
};
struct big {
    const long *p;
    long a, b, c;
};
struct A {
    long a = 1;
    virtual self f(long x) = 0;
    virtual big g(big in) = 0;
};
struct B {
    long b = 2;
    virtual self h(self s, long x) = 0;
    virtual self f(long x) = 0;
    virtual big g(big in) = 0;
};
struct C : A, B {
    long c = 3;
    self f(long x) override { return self(x + a + b + c); }
    self h(self s, long x) override { return self(s.v + x + c); }
    big g(big in) override { return big{&c, in.a + c, 2, 3}; }
};
struct V {
    long v = 100;
    virtual self f(long x) { return self(x + v); }
};
struct D : virtual V {
    long d = 7;
    self f(long x) override { return self(x + d + v); }
};
int main()
{
    C c;
    B *b = &c;
    D d;
    V *v = &d;
    {
        self r = b->f(10);
        printf("%d %ld\n", r.me == &r, r.v);
        self s = b->h(self(5), 1);
        printf("%d %ld\n", s.me == &s, s.v);
        big g = b->g(big{0, 40, 0, 0});
        printf("%ld %ld\n", *g.p, g.a);
        self t = v->f(1);
        printf("%d %ld\n", t.me == &t, t.v);
    }
}
