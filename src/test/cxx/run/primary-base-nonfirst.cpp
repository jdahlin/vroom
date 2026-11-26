// Dynamic class whose primary base is not its first direct base:
// [abi:class-types] I/II places the first non-virtual dynamic base at offset 0.

struct EmptyBase {};

struct NonPoly {
    int a;
    int b;
    NonPoly(int a, int b) : a(a), b(b) {}
};

struct Poly {
    int p;
    Poly(int p) : p(p) {}
    virtual ~Poly() {}
    virtual int value() const { return p; }
    virtual int compute(int x) const { return p + x; }
};

struct DerivedFromEmpty : EmptyBase, Poly {
    int d;
    DerivedFromEmpty(int p, int d) : Poly(p), d(d) {}
    int value() const override { return p + d; }
    int compute(int x) const override { return p * x + d; }
};

struct DerivedFromNonPoly : NonPoly, Poly {
    int d;
    DerivedFromNonPoly(int a, int b, int p, int d)
        : NonPoly(a, b), Poly(p), d(d) {}
    int value() const override { return a + b + p + d; }
    int compute(int x) const override { return (a + b) * x + p + d; }
};

int call_poly(const Poly *p) { return p->value() + p->compute(3); }

int main()
{
    DerivedFromEmpty de(10, 5);
    Poly *pe = &de;
    if (static_cast<void *>(pe) != static_cast<void *>(&de))
        return 1;
    if (pe->value() != 15)
        return 2;
    if (pe->compute(4) != 45)
        return 3;
    if (call_poly(&de) != 15 + 35)
        return 4;

    DerivedFromNonPoly dn(2, 3, 10, 7);
    Poly *pn = &dn;
    NonPoly *np = &dn;
    if (static_cast<void *>(pn) != static_cast<void *>(&dn))
        return 5;
    if (static_cast<void *>(np) == static_cast<void *>(&dn))
        return 6;
    if (np->a != 2 || np->b != 3)
        return 7;
    if (pn->p != 10 || dn.d != 7)
        return 8;
    if (pn->value() != 22)
        return 9;
    if (pn->compute(4) != 37)
        return 10;
    if (call_poly(&dn) != 22 + 32)
        return 11;

    return 0;
}
