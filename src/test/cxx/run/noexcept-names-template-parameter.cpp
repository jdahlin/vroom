/* [class.mem]/6, [temp.local]/6: a noexcept-specifier naming a namespace
   member that another member template of the class has as its own
   template template-parameter, with a default argument naming it again */
template <class... T>
struct all_of {
    static const bool value = true;
};
template <class T>
struct nothrow {
    static const bool value = true;
};
template <class... T>
struct holder {
    int v;
    holder() : v(1) {}
    holder &operator=(const holder &o) noexcept(all_of<nothrow<T>...>::value)
    {
        v = o.v + 1;
        return *this;
    }
    template <template <class...> class all_of = all_of, class U = int>
    holder(U u, int) noexcept(all_of<nothrow<T>...>::value) : v(u)
    {
    }
};
int main()
{
    holder<int> a;
    holder<int> b;
    b = a;
    if (b.v != 2)
        return 1;
    return 0;
}
