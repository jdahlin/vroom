/* [temp.inst], [temp.point]: the definition of a const member function
   template, instantiated after the translation unit's declarations, finds
   one more template by argument-dependent lookup in its
   trailing-return-type: declaring that one's specialization leaves the
   member's instantiation the one being made (its const is not misplaced,
   [dcl.fct]/6; json's from_json_fn) */
namespace udt
{
struct X {
    int v;
};
int from(int n, X &out)
{
    out.v = n;
    return 1;
}
} // namespace udt
namespace lib
{
struct from_fn {
    template <class T>
    auto operator()(int n, T &val) const
        noexcept(noexcept(from(n, val))) -> decltype(from(n, val))
    {
        return from(n, val);
    }
};
} // namespace lib

static int run()
{
    udt::X x{0};
    lib::from_fn f;
    if (f(5, x) != 1 || x.v != 5)
        return 1;
    return 0;
}

namespace udt
{
template <class... U> int from(U &&...)
{
    return 2;
}
} // namespace udt

int main()
{
    return run();
}
