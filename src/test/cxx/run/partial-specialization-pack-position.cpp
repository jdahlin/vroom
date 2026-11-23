/* [temp.param]/14: a partial specialization's template parameter pack
   need not be its last template-parameter (libc++'s __find_first:
   template <class _Head, class... _Tail, size_t _Size>) */
typedef unsigned long size_t;
template <class... T> struct tl {};
template <class> struct head;
template <class H, class... T> struct head<tl<H, T...> > { using type = H; };
template <class L, size_t S, bool = S <= sizeof(typename head<L>::type)> struct ff;
template <class H, class... T, size_t S> struct ff<tl<H, T...>, S, true> { using type = H; };
template <class H, class... T, size_t S> struct ff<tl<H, T...>, S, false> {
  using type = typename ff<tl<T...>, S>::type;
};
template <class L, size_t S> constexpr size_t sz = sizeof(typename ff<L, S>::type);
static_assert(sz<tl<char, short, int, long>, 4> == 4, "");
static_assert(sz<tl<char, long>, 1> == 1, "");

/* the pack between two deduced parameters */
template <class A, class B> struct pair_of {};
template <class> struct split;
template <class H, class... M, class Last>
struct split<pair_of<tl<H, M...>, Last> > {
    enum { middle = sizeof...(M), last = sizeof(Last) };
};

int main()
{
    if (sz<tl<char, long>, 2> != 8)
        return 1;
    if (split<pair_of<tl<int, char, char>, short> >::middle != 2 ||
        split<pair_of<tl<int>, long> >::last != 8)
        return 2;
    return 0;
}
