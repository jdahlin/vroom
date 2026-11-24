// [temp.dep.candidate]/1: a dependent call's candidates are the
// declarations visible from the template's definition and those
// argument-dependent lookup finds at the point of instantiation.  Every
// `twice` below is declared after the template: ordinary lookup from the
// definition finds none, and ADL finds each in its argument's namespace.
template <class T> int use(const T &t)
{
    return twice(t);
}

namespace a {
struct S {
    int v;
};
int twice(const S &s)
{
    return 2 * s.v;
}
} // namespace a

namespace b {
struct S {
    int v;
};
int twice(const S &s)
{
    return 20 * s.v;
}
} // namespace b

int main()
{
    a::S x{1};
    b::S y{2};
    return use(x) + use(y) == 42 ? 0 : 1;
}
