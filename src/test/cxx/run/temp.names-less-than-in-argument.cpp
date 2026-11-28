/* [temp.names]/3: in a template argument, a < after a ')' or after a name
   declared as a variable is the less-than operator, not the start of
   another template-argument-list */
template <bool B, class A> struct pick {
    static constexpr int value = B;
};
constexpr int x = 1;
template <class Rep, int N = 2> struct F {
    using rep = pick<sizeof(Rep) < sizeof(int), int>;
    using small = pick<Rep(1) && sizeof(Rep) < 4, int>;
    using named = pick<x < 4, int>;
    using parameter = pick<N < 1, int>;
};
int main()
{
    if (F<char>::rep::value != 1 || F<long>::rep::value != 0)
        return 1;
    if (F<short>::small::value != 1 || F<int>::small::value != 0)
        return 2;
    if (F<int>::named::value != 1 || F<int>::parameter::value != 0)
        return 3;
    return 0;
}
