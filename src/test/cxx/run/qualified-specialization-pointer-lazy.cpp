// [temp.inst]/2: a qualified template-id named where no complete type is
// needed (a pointer to it, a template argument) does not instantiate the
// specialization: ns::Strict<unsigned char> would be ill-formed (its
// static_assert fails) were it defined.
namespace ns {
template <class C> struct Traits;
template <> struct Traits<char> {
    typedef char char_type;
};
template <class C> struct Strict {
    static_assert(sizeof(typename Traits<C>::char_type) == 1, "needs traits");
    C c;
};
template <class S> struct Holder {
    S *p;
};
} // namespace ns
ns::Strict<unsigned char> *g;
ns::Holder<ns::Strict<unsigned char>> h;
ns::Strict<char> ok;
int main()
{
    if (g != 0 || h.p != 0)
        return 1;
    ok.c = 'x';
    if (ok.c != 'x')
        return 2;
    return 0;
}
