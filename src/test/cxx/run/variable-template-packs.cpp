/* [temp.variadic]: variable templates whose last parameter is a pack,
   used with packs of known elements (libc++'s is_constructible_v) */
template <class... A> constexpr int count_v = sizeof...(A);
template <class T, class... A> constexpr int first_size_v = sizeof(T);
template <int... N> constexpr int sum_v = (0 + ... + N);

static_assert(count_v<> == 0, "");
static_assert(count_v<int, char> == 2, "");
static_assert(first_size_v<long, char, short> == 8, "");
static_assert(sum_v<1, 2, 3> == 6, "");

template <class... A> struct S {
    static int g() { return count_v<A..., int>; }
};

int main()
{
    if (S<char, long>::g() != 3)
        return 1;
    return sum_v<4, 5> == 9 ? 0 : 2;
}
