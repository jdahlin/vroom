/* [basic.lookup.argdep]/4: argument-dependent lookup takes a namespace's
   functions along with its function templates, also when unqualified
   lookup found a template of its own (json's to_json beside a user's
   to_json template) */
namespace lib
{
struct J {
    int v;
};
namespace detail
{
template <class B> void to_json(B &j, int)
{
    j.v = 100;
}
struct fn {
    template <class T> void operator()(J &j, T &&t) const
    {
        to_json(j, static_cast<T &&>(t));
    }
};
} // namespace detail
} // namespace lib
namespace udt
{
struct person {
    int age;
};
template <class B> static void to_json(B &j, const person &p)
{
    j.v = p.age;
}
struct address {
    int m;
};
static void to_json(lib::J &j, const address &a)
{
    j.v = a.m;
}
} // namespace udt

int main()
{
    udt::address a{3};
    const udt::address &c = a;
    udt::person p{4};
    lib::J j{0};
    lib::detail::fn()(j, c);
    if (j.v != 3)
        return 1;
    lib::detail::fn()(j, p);
    if (j.v != 4)
        return 2;
    lib::detail::fn()(j, 0);
    if (j.v != 100)
        return 3;
    return 0;
}
