/* [impl.clang] a function template's requires-clause after its
   template-args, Q <expression> E:
   - Q<args>::name, a class template's member: sr <level> E <name>;
   - ns::v<args>, a variable template of a namespace (std's inline
     namespace not written): sr 3std E <name> <args>;
   - in a member template of a class template, the template's own
     parameters at their level, TL0__ and TL0_0_, each a candidate of its
     own (a second C<I> is S<n>_), not the T_ of the function's types
     (SkSpan's requires std::is_integral_v<Integer> constructor) */
namespace std {
inline namespace __1 {
template <class T> inline constexpr bool is_integral_v = false;
template <> inline constexpr bool is_integral_v<int> = true;
template <> inline constexpr bool is_integral_v<long> = true;
} // namespace __1
} // namespace std

template <class T> struct is_int {
    static constexpr bool value = std::is_integral_v<T>;
};
template <class T> concept C = sizeof(T) >= 1;
template <class T> concept E = sizeof(T) <= 8;

template <class I>
    requires is_int<I>::value
long free_member(I s)
{
    return s;
}

template <class I>
    requires std::is_integral_v<I>
long free_namespace(I s)
{
    return s + 1;
}

struct Plain {
    long n;
    template <class I>
        requires is_int<I>::value
    Plain(I s) : n(s)
    {
    }
};

template <class T> struct Span {
    T *p;
    unsigned long n;
    template <class I>
        requires std::is_integral_v<I>
    constexpr explicit(false) Span(T *q, I s) : p(q), n(s)
    {
    }
    template <class I>
        requires is_int<I>::value
    long member(I s)
    {
        return s + 2;
    }
    template <class I, class J>
        requires C<I> && E<J> && C<I>
    long twice(I s, J)
    {
        return s + 3;
    }
    template <class I>
        requires C<I>
    long concept_member(I s)
    {
        return s + 4;
    }
};

int main()
{
    int a[3] = {1, 2, 3};
    Span<int> s(a, 3);
    if (free_member(1) != 1 || free_namespace(1L) != 2 || Plain(5).n != 5)
        return 1;
    if (s.n != 3 || s.member(1) != 3 || s.twice(1, 'c') != 4 ||
        s.concept_member(1) != 5)
        return 2;
    return 0;
}
