/* [dcl.init.aggr]/4.2, [dcl.array]/3, [temp.variadic]/7: the bound of an
   array of unknown bound is the number of initializer-clauses after pack
   expansion; constexpr arrays of enumerations and of constants computed
   by constexpr calls (libc++'s common_comparison_category). */
enum class kind : unsigned char { a, b, c };
template <class T> constexpr int size() { return sizeof(T); }
template <class T> constexpr kind to_kind()
{
    if (sizeof(T) == 1)
        return kind::b;
    return kind::c;
}
template <class... Ts> constexpr int count()
{
    constexpr int a[] = {0, size<Ts>()...};
    return sizeof(a) / sizeof(a[0]);
}
template <class... Ts> constexpr int sum()
{
    constexpr int a[] = {0, size<Ts>()...};
    int s = 0;
    for (unsigned i = 0; i < sizeof(a) / sizeof(a[0]); i++)
        s += a[i];
    return s;
}
template <class... Ts> constexpr int kinds()
{
    constexpr kind k[] = {kind::a, to_kind<Ts>()...};
    int n = 0;
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        n += static_cast<int>(k[i]);
    return n;
}
template <class... Ts> constexpr int only()
{
    constexpr int a[] = {size<Ts>()...};
    return sizeof(a) / sizeof(a[0]);
}
template <class... Ts> constexpr int rows()
{
    constexpr int a[][2] = {{size<Ts>(), 1}...};
    int s = 0;
    for (unsigned i = 0; i < sizeof(a) / sizeof(a[0]); i++)
        s += a[i][0] * 10 + a[i][1];
    return s;
}
constexpr int twice(int x) { return 2 * x; }
constexpr int plain()
{
    constexpr int a[] = {twice(1), 2, twice(3)};
    return a[0] + a[2] + sizeof(a) / sizeof(a[0]);
}
static_assert(count<>() == 1);
static_assert(count<int>() == 2);
static_assert(count<int, char, long>() == 4);
static_assert(sum<int, char, long>() == 13);
static_assert(kinds<>() == 0);
static_assert(kinds<char, int, char>() == 1 + 2 + 1);
static_assert(only<int, char>() == 2);
static_assert(plain() == 11);
static_assert(rows<int, char>() == 41 + 11);
int main()
{
    /* (constants: no call is emitted, as clang's -O0 emits none) */
    constexpr int n = count<int, char>() + kinds<char>() + plain();
    if (n == 3 + 1 + 11)
        return 0;
    return 1;
}
