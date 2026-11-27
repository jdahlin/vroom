/* [class.copy.elision]/1 with [expr.mptr.oper]: a class returned by address
   through a pointer to member function, or by a virtual call whose object is
   held in a variable, is built in the object it initializes. */
extern "C" int printf(const char *, ...);
int live, copies;
struct Self {
    Self *me; int v;
    Self(int v) : me(this), v(v) { live++; }
    Self(const Self &o) : me(this), v(o.v) { live++; copies++; }
    ~Self() { live--; }
    bool ok() const { return me == this; }
};
struct Big { Big *me; long pad[3]; bool ok() const { return me == this; } };
struct P {
    int base = 10;
    Self plain(int a) { return Self(base + a); }
    virtual Self virt(int a) { return Self(base * a); }
    Big big(int) { Big b; b.me = &b; return b; }
    Self run(Self (P::*f)(int), int a) { return (this->*f)(a); }
};
struct Q : P { Self virt(int a) override { return Self(base * a + 1); } };
static Q &held()
{
    static Q *q = new Q;
    return *q;
}
int main()
{
    int bad = 0;
    {
        Q q; P *p = &q;
        Self (P::*f)(int) = &P::plain;
        Self (P::*g)(int) = &P::virt;
        Self h = held().virt(1);
        Self i = held().virt(0);
        bad += !h.ok() + !i.ok();
        Self a = (p->*f)(1);
        Self b = (q.*g)(2);
        Self c = p->run(f, 3);
        bad += !a.ok() + !b.ok() + !c.ok();
        printf("%d %d %d %d\n", a.v, b.v, c.v, (p->*g)(3).v);
        bad += !(p->*f)(4).ok();
        for (int i = 0; i < 3; i++) { const Self &r = (p->*f)(i); bad += !r.ok(); }
    }
    printf("%d %d %d\n", bad, live, copies);
    return bad || live;
}
