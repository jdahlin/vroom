/* [over.match.best]/2.8 with variadic templates: a non-rewritten operator<
   and a rewritten operator<=> (normal and reversed) on variadic class
   template specializations (like std::variant and std::tuple); the
   non-rewritten one is chosen. [temp.func.order]/3 */
template <class... Ts> struct var {
    int v;
};

template <class... Ts>
constexpr bool operator<(const var<Ts...> &x, const var<Ts...> &y)
{
    return x.v < y.v;
}

template <class... Ts>
constexpr int operator<=>(const var<Ts...> &x, const var<Ts...> &y)
{
    return 100;
}

template <class... Ts> struct tup {
    int v;
};

template <class... Ts, class... Us>
constexpr bool operator<(const tup<Ts...> &x, const tup<Us...> &y)
{
    return x.v < y.v;
}

template <class... Ts, class... Us>
constexpr int operator<=>(const tup<Ts...> &x, const tup<Us...> &y)
{
    return 100;
}

constexpr var<int, long, short> va{2};
constexpr var<int, long, short> vb{5};
static_assert(va < vb);
static_assert(!(vb < va));

int main()
{
    tup<int, long> c{3};
    tup<short, char> d{7};
    if (!(c < d))
        return 1;
    if (d < c)
        return 2;
    return 0;
}
