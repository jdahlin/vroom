/* [temp.mem]/1, [temp.pre]/2: a static data member template, constexpr and inline, of a class */
struct S {
    template <class T> static constexpr int size = sizeof(T);
    template <class T> static inline T zero = T();
};
static_assert(S::size<char> == 1 && S::size<long> == 8, "");
int main()
{
    S::zero<int> = 3;
    S s;
    return S::zero<int> - 3 + S::zero<long> + s.size<short> - 2;
}
