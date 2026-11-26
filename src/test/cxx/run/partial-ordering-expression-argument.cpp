/* [temp.func.order]: Vec<T, 8 / sizeof(T)> is more specialized than a
   template parameter alone (Highway's DupEven(Vec64<T>)) */
template <class T, int N> struct Vec {
    T raw;
};
template <class T> using Vec64 = Vec<T, 8 / sizeof(T)>;
template <bool B> struct En {};
template <> struct En<true> {
    using type = void;
};
template <bool B> using EI = typename En<B>::type;

template <class T, EI<sizeof(T) == 2> * = nullptr> int f(const Vec64<T>)
{
    return 1;
}
template <class V, EI<(sizeof(V) > 1)> * = nullptr> int f(const V) { return 2; }

int main() { return f(Vec<short, 4>()) != 1 || f(Vec<int, 4>()) != 2; }
