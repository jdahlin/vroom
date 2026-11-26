/* [temp.func.order]: a parameter that is Q::name or decltype(...) is a
   non-deduced context; the template whose parameter can be deduced from is
   the more specialized (Highway's TruncateTo overloads) */
template <class T, int N> struct Vec {};
template <class T, int N> struct S {};
template <class T> struct M {
    using type = Vec<unsigned, 1>;
};
template <class D> Vec<unsigned, 1> Zero(D);

template <class D> using Member = typename M<D>::type;
template <class D> using Decl = decltype(Zero(D()));

template <class F, class D> int g(D, Vec<F, 1>) { return 1; }
template <class D> int g(D, Member<D>) { return 2; }
template <class F, class D> int h(D, Vec<F, 1>) { return 3; }
template <class D> int h(D, Decl<D>) { return 4; }

int main()
{
    S<short, 1> d;
    return g(d, Vec<unsigned, 1>()) != 1 || h(d, Vec<unsigned, 1>()) != 3 ||
           g(d, Vec<int, 1>()) != 1;
}
