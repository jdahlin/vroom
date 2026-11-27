/* [temp.class.order]: partial specializations that each end in a pack
   expansion (SkZip's PickOneSize). */
extern "C" int printf(const char *, ...);

template <class C, class... Ts> struct pick {
    static const int k = 0;
};
template <class T, class... Ts> struct pick<T *, Ts...> {
    static const int k = 1;
};
template <class T, class... Ts, unsigned long N> struct pick<T (&)[N], Ts...> {
    static const int k = 2 + N;
};
template <class C, class... Ts> struct pick<C &, Ts...> {
    static const int k = 3;
};

static_assert(pick<int *, int>::k == 1, "");
static_assert(pick<int &>::k == 3, "");
static_assert(pick<int (&)[3], int &, char>::k == 5, "");

int main()
{
    printf("%d %d %d %d\n", pick<int>::k, pick<int *>::k,
           pick<char &, int *>::k, pick<int (&)[2], int>::k);
    return 0;
}
