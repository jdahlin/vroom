/* [temp.constr.decl]/[temp.deduct]/5: a constructor template with a
   requires-clause after its template-head ([temp.pre]/1) is a candidate only
   when its constraints are satisfied by the deduced arguments: the views of
   the C++ front end's syntax tree convert from a node of their kind this way */
template <class From, class To> inline constexpr bool converts = false;
struct A {};
struct B;
struct D;
template <> inline constexpr bool converts<A, B> = true;

struct B {
    int how = 0;
    B() = default;
    template <class V>
        requires converts<V, B>
    B(V) : how(1)
    {
    }
    /* the fallback the constraint leaves to everything else */
    B(const D &) : how(2)
    {
    }
};
struct D {};
struct E {};

int main()
{
    A a;
    D d;
    B x(a);
    B y(d);
    B z;
    if (x.how != 1)
        return 1;
    if (y.how != 2)
        return 2;
    return z.how;
}
