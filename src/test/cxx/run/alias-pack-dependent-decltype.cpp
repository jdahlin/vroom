// [temp.dep.type]/10.3: an alias template whose decltype operand names a
// pack, used with arguments that are another template's parameters, is a
// dependent type, not analysed until instantiation.
template <class T> struct is_a { static constexpr bool value = true; };
template <bool B, class T = void> using eit = T;
double pick(double);
template <class... A>
using P = decltype((eit<(is_a<A>::value && ...)>)0, (pick(A()) + ...));

template <class A, class B> struct R {
    using type = P<A, B>;
    static constexpr bool same = __is_same(P<A, B>, double);
};
template <class A, class B> P<A, B> add(A a, B b);

static_assert(R<int, float>::same);
static_assert(__is_same(R<int, double>::type, double));
static_assert(__is_same(decltype(add(1, 2.0)), double));

int main()
{
    return 0;
}
