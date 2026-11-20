// A member or base subobject of a class that copies its bytes, initialized
// from a prvalue or a reference to another class with an explicit conversion
// function ([dcl.init]/17.6.1, [over.match.copy]/1.2, [class.base.init]/9.1)
struct Index {
    unsigned i = 0;
    constexpr Index() = default;
    explicit constexpr Index(unsigned i) : i(i) {}
};
struct Child {
    unsigned bits = 0;
    explicit operator bool() const { return bits != 0; }
};
struct Ref {
    Ref() = default;
    Ref(Child c)
    {
        if (c.bits & 1)
            n = Index(c.bits >> 1);
    }
    Ref(Index n) : n(n) {}
    explicit operator bool() const { return n.i != 0; }
    Index n;
};
struct Element : Ref {
    Element(Child c) : Ref(c), c(c) {}
    Element(const Ref &r) : Ref(r), c(make()) {}
    Child c;
    static Child make()
    {
        Child k;
        k.bits = 9;
        return k;
    }
};
int main()
{
    Child k;
    k.bits = 7;
    Element e(k);
    if (e.n.i != 3 || e.c.bits != 7)
        return 1;
    Element f(static_cast<const Ref &>(e));
    if (f.n.i != 3 || f.c.bits != 9)
        return 2;
    return 0;
}
