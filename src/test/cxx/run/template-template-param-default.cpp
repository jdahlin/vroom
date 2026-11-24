// [temp.param]/17: a template parameter of a template template parameter
// may have a default template argument, which applies to the template
// template parameter in its scope.
template <typename T, typename S> struct B { char t[sizeof(S)]; };
template <template <typename T, typename S = long> class C> struct D {
    C<int> c;
};
template <template <typename T, typename S = void *> class C = B>
struct E {
    C<int> c;
};
static_assert(sizeof(D<B>) == sizeof(long), "ttp default");
static_assert(sizeof(E<>) == sizeof(void *), "ttp default, defaulted argument");
int main()
{
    D<B> d;
    E<> e;
    if (sizeof d.c.t != sizeof(long))
        return 1;
    if (sizeof e.c.t != sizeof(void *))
        return 2;
    return 0;
}
