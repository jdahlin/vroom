/* [abi:mangle.name]: typename decltype(e)::name, through aliases, in a
   non-type template parameter's type and in a parameter's (Highway's
   TFromD<DFromV<V>>) */
template <bool B> struct En {};
template <> struct En<true> {
    using type = void;
};
template <bool B> using EI = typename En<B>::type;
template <class T> struct Simd {
    using Lane = T;
};
template <class V> Simd<typename V::Raw> From(V);
template <class V> using DFromV = decltype(From(V()));
template <class D> using TFromD = typename D::Lane;
struct Vec {
    using Raw = short;
};

template <class V, EI<(sizeof(TFromD<DFromV<V>>) == 2)> * = nullptr> int f(V)
{
    return 1;
}
template <class V> int g(V, TFromD<DFromV<V>> x) { return x; }

int main() { return f(Vec()) + g(Vec(), 2) != 3; }
