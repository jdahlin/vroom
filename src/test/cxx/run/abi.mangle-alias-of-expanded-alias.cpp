// [abi:mangle.template-arg]: libc++'s tuple operator<=>: an alias template
// whose pack takes the expansion of another alias's specialization, in the
// return type of a constrained template; sizeof... in its requires-clause
// and a pack expansion substituted a second time.
/* cxx-run-flags: -std=c++20 */
template <class... T> struct category {
    using type = int;
};
template <class... T> using category_t = typename category<T...>::type;
template <class... T> struct Many {
};
template <class T> T &&declared();
namespace s {
struct Synth {
    template <class T, class U> int operator()(const T &, const U &) const;
};
inline constexpr Synth synth{};
template <class T, class U = T>
using result = decltype(s::synth(declared<T &>(), declared<U &>()));
}

namespace q {
template <class... T, class... U>
    requires(sizeof...(T) == sizeof...(U))
category_t<s::result<T, U>...> compare(const Many<T...> &, const Many<U...> &)
{
    return sizeof...(T);
}
template <class... T> category_t<T...> same(Many<T...>) { return 1; }
} // namespace q

int main()
{
    if (q::compare(Many<int, long>(), Many<int, int>()) != 2)
        return 1;
    if (q::same(Many<int, char>()) != 1)
        return 2;
    return 0;
}
