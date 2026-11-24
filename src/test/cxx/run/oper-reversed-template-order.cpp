/* [over.match.best]/2.8 with templates: a non-rewritten operator> and a
   rewritten operator<=> (normal and reversed) on class template
   specializations; the non-rewritten one is chosen. [temp.func.order]/3 */
template <int R> struct ratio {};
template <class A, class B> struct dur { long v; };

template <class A, class B, class C, class D>
constexpr bool operator>(const dur<A, B> &x, const dur<C, D> &y)
{
    return y.v < x.v;
}
template <class A, class B, class C, class D>
constexpr int operator<=>(const dur<A, B> &x, const dur<C, D> &y)
{
    return 100;
}

int main()
{
    dur<long, ratio<1>> a{5};
    dur<int, ratio<2>> b{2};
    if (!(a > b))
        return 1;
    if (b > a)
        return 2;
    return 0;
}
