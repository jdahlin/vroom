/* [temp.deduct.partial]/8, [temp.func.order]: a parameter of a
   specialization with no arguments, S<>, is more specialized than S<I...>:
   deducing S<> from S<unique...> fails (json's to_json_tuple_impl for an
   empty tuple) */
template <int... I> struct seq {
};
template <class J, class T, int... I>
int impl(J &, const T &, seq<I...>)
{
    return 1;
}
template <class J, class T> int impl(J &, const T &, seq<>)
{
    return 2;
}

int main()
{
    int j = 0;
    if (impl(j, j, seq<>()) != 2)
        return 1;
    if (impl(j, j, seq<1, 2>()) != 1)
        return 2;
    return 0;
}
