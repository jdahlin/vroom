// [temp.mem]/1 with [class.conv.fct]/1: a conversion function template
// declared in a class template with a requires-clause, as libc++'s
// __in_out_result in <__algorithm/in_out_result.h>; it is only declared
// (no deduction of its template arguments, [temp.deduct.conv], is needed
// by the program).
template <class T, class U> concept Conv = __is_convertible_to(T, U);

template <class A, class B>
struct Pair {
    A first;
    B second;
    template <class A2, class B2>
        requires Conv<A, A2> && Conv<B, B2>
    constexpr operator Pair<A2, B2>() &&
    {
        return {first, second};
    }
    template <class T>
    explicit operator T*() const { return 0; }
};

int main()
{
    Pair<int, int> p{3, 4};
    return p.first + p.second == 7 ? 0 : 1;
}
