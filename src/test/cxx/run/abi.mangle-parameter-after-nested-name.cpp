/* a non-type parameter's type naming an earlier parameter, after an
   argument whose name has a function template specialization in it */
template <bool B> struct en {};
template <> struct en<true> {
    typedef int type;
};
template <class T> auto make()
{
    struct L {
        T v = 1;
    };
    return L();
}
template <class F, bool M, typename en<M && true>::type = 0> int f(F x)
{
    return x.v;
}
int main()
{
    auto l = make<int>();
    return f<decltype(l), true>(l) - 1;
}
