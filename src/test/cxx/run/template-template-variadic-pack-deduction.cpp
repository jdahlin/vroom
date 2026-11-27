// RUN: %cxx-run

template <typename... Ts> struct MyTuple {
    static constexpr int count = sizeof...(Ts);
};

template <typename A, typename B, typename C> struct MyTriple {
    static constexpr int count = 3;
};

template <typename A, typename... Bs> struct MyMixed {
    static constexpr int count = 1 + sizeof...(Bs);
};

struct Cost {};
struct Flags {};

template <typename T> struct Helper;

template <template <typename...> typename Tret, typename... Tretargs,
          typename... Targs>
struct Helper<Tret<Cost, Tretargs...> (*)(Flags, Targs...)> {
    using RetTuple = Tret<Cost, Tretargs...>;
    static constexpr int ret_extra = sizeof...(Tretargs);
    static constexpr int arg_extra = sizeof...(Targs);
};

template <typename T> struct HeadPackHelper;

template <template <typename, typename...> typename TT, typename Head,
          typename... Tail>
struct HeadPackHelper<TT<Head, Tail...>> {
    static constexpr int tail_count = sizeof...(Tail);
};

MyTuple<Cost, int, double> fn_tuple(Flags, int, bool);
MyTriple<Cost, char, short> fn_triple(Flags, long);
MyMixed<Cost, int, long, double> fn_mixed(Flags);

int main()
{
    if (Helper<decltype(&fn_tuple)>::ret_extra != 2)
        return 1;
    if (Helper<decltype(&fn_tuple)>::arg_extra != 2)
        return 2;
    if (Helper<decltype(&fn_tuple)>::RetTuple::count != 3)
        return 3;

    if (Helper<decltype(&fn_triple)>::ret_extra != 2)
        return 4;
    if (Helper<decltype(&fn_triple)>::arg_extra != 1)
        return 5;
    if (Helper<decltype(&fn_triple)>::RetTuple::count != 3)
        return 6;

    if (Helper<decltype(&fn_mixed)>::ret_extra != 3)
        return 7;
    if (Helper<decltype(&fn_mixed)>::arg_extra != 0)
        return 8;
    if (Helper<decltype(&fn_mixed)>::RetTuple::count != 4)
        return 9;

    if (HeadPackHelper<MyMixed<Cost, int, double>>::tail_count != 2)
        return 10;
    if (HeadPackHelper<MyTriple<Cost, int, double>>::tail_count != 2)
        return 11;

    return 0;
}
