/* [temp.dep.type]/10.3: decltype of a type-dependent expression in a
   template's declaration, and [temp.over]/1 with explicit
   template-arguments for several function templates (libc++'s
   declval) */
namespace s {
template <class T> T &&dv(int);
template <class T> T dv(long);
template <class T> decltype(s::dv<T>(0)) declval() noexcept;

template <class T> int pick(T) { return 1; }
template <class T, class U> int pick(U, U) { return 2; }
template <int N> int pick(long) { return 3 + N; }
} // namespace s

static_assert(sizeof(s::declval<int>()) == 4, "");
static_assert(sizeof(s::declval<char>()) == 1, "");
typedef decltype(s::declval<int>()) r;
r f(int &&x) { return static_cast<int &&>(x); }

int main()
{
    int v = 7;
    if (f(static_cast<int &&>(v)) != 7)
        return 1;
    if (s::pick<int>(1) != 1)
        return 2;
    if (s::pick<int, int>(1, 2) != 2)
        return 3;
    if (s::pick<4>(1L) != 7)
        return 4;
    return 0;
}
