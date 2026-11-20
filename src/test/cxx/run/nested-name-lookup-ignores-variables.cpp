/* [basic.lookup.qual]/1: "lookup of the name preceding that :: considers
   only namespaces, types, and templates whose specializations are types":
   a variable or parameter of the same name, found first by an unqualified
   lookup, is ignored before ::, as libc++'s <set> writes
   `typename __set::__base` in a function whose parameter is __set. */
struct T {
    using type = int;
    static constexpr int count = 3;
};

namespace N {
struct Inner {
    using type = long;
};
} // namespace N

int shadowed_by_parameter(int T)
{
    T::type x = T;
    return x + T::count;
}

int namespace_shadowed(int N)
{
    N::Inner::type y = N;
    return (int)y;
}

int shadowed_by_local()
{
    int T = 4;
    T::type x = T;
    return x + T::count;
}

int main()
{
    if (shadowed_by_parameter(2) != 5)
        return 1;
    if (namespace_shadowed(7) != 7)
        return 2;
    if (shadowed_by_local() != 7)
        return 3;
    return 0;
}
