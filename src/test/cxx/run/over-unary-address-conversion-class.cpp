/* [over.match.oper]/9: with no viable operator function, the unary
   operator & is the built-in one, also for a class with conversion
   functions and an operator& that does not apply (json_ref's &**this once
   argument-dependent lookup reaches std's operator&) */
namespace lib
{
enum class bits : int { one = 1 };
bits operator&(bits a, bits b)
{
    return a;
}
struct J {
    int v;
    template <class T> operator T() const
    {
        return T(v);
    }
};
} // namespace lib

struct Ref {
    lib::J j;
    const lib::J &operator*() const
    {
        return j;
    }
    const lib::J *operator->() const
    {
        return &**this;
    }
};

int main()
{
    Ref r{{4}};
    if (r->v != 4)
        return 1;
    const lib::J *p = &*r;
    if (p != &r.j)
        return 2;
    return 0;
}
