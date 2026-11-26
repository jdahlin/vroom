/* [abi:expressions] alias templates, type keywords and class names among
   the template arguments and functional casts of dependent expressions,
   and a conditional (Highway's HWY_IF_* signatures) */
template <bool B> struct En {};
template <> struct En<true> {
    using type = void;
};
template <bool B> using EI = typename En<B>::type;
namespace hwy
{
template <class A, class B = int> constexpr bool Is() { return true; }
template <class T> struct Id {
    using type = T;
};
template <class T> using Rm = typename Id<T>::type;
} // namespace hwy
struct S {
    constexpr unsigned long L() const { return 4; }
};
template <class D> using Re = D;
template <int N> struct W {};

template <class D, EI<hwy::Is<D, float>()> * = nullptr> int kw(D) { return 1; }
template <class D, EI<hwy::Is<hwy::Rm<D>, float>()> * = nullptr> int al(D)
{
    return 2;
}
template <class D, EI<hwy::Is<hwy::Rm<D>, S>()> * = nullptr> int cl(D)
{
    return 3;
}
template <class D> int cast(W<Re<D>().L()>, D) { return 4; }
template <int N> int cond(W<N ? 3 : 2>) { return 5; }

int main()
{
    S s;
    int qualified(), literals();
    if (literals())
        return 2;
    return kw(s) + al(s) + cl(s) + cast(W<4>(), s) + cond<1>(W<3>()) +
               qualified() !=
           17;
}

/* ns::Alias<T>::member keeps the alias as written */
struct K {
    static constexpr int k = 4;
};
namespace h
{
namespace n
{
template <class V> using D = V;
} // namespace n
} // namespace h
template <class V, EI<h::n::D<V>::k == 4> * = nullptr> int outer(V) { return 2; }

int qualified() { return outer(K()); }

/* integer-literals of every base and suffix, and T{n} of a typedef-name */
typedef unsigned long size_type;
template <class T, int K> W<(K << 8) + 0x200> *hex(T) { return nullptr; }
template <class T> W<sizeof(T) == 0x10u ? 07 : 2ULL> *suffixes(T)
{
    return nullptr;
}
template <class T, EI<(sizeof(T) > size_type{1})> * = nullptr> int braced(T)
{
    return 0;
}

int literals() { return !hex<int, 1>(1) + !suffixes(1) + braced(1) - 2; }
