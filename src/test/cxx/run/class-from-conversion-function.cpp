/* [over.match.copy]/1.2, [dcl.init]/17.6.3: a class initialized from an
   object of another class whose conversion function yields it, as a
   constructor's argument and directly; [over.match.best]: a derived-to-base
   binding beats the user-defined conversion; [over.match.funcs]/8: a base's
   copy constructor is not inherited.  The syntax tree's SyntaxElement and
   SyntaxRef views. */
struct child {
    unsigned bits = 0;
    explicit operator bool() const
    {
        return bits != 0;
    }
};
child make(unsigned b)
{
    child c;
    c.bits = b;
    return c;
}
struct Ref {
    unsigned n = 0;
    Ref() = default;
    Ref(child c) : n(c.bits)
    {
    }
    Ref(const Ref &) = default;
    explicit operator bool() const
    {
        return n != 0;
    }
};
struct Elem : Ref {
    child c;
    Elem() = default;
    explicit Elem(unsigned b) : Ref(make(b)), c(make(b))
    {
    }
    operator child() const
    {
        return c;
    }
    explicit operator bool() const
    {
        return c.bits != 0;
    }
};
struct Base : Ref {
    using Ref::Ref;
};
template <class V> struct Extra : Base {
    using Base::Base;
};
struct V1 : Extra<V1> {
    using Extra<V1>::Extra;
    V1(const Elem &e) : Extra<V1>(e)
    {
    }
};

int main()
{
    Elem e(7);
    child c = e;
    Base b(e);
    V1 v(e);
    V1 w = V1(Elem(3));
    Elem none;
    if (c.bits != 7)
        return 1;
    if (b.n != 7)
        return 2;
    if (v.n != 7 || w.n != 3)
        return 3;
    if (!e || none)
        return 4;
    return 0;
}
