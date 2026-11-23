// Pack expansion in explicit template arguments of an overloaded function
// template call ([temp.variadic]/5, [temp.over]/1), as in libc++'s _And.

struct true_type { static constexpr bool value = true; };
struct false_type { static constexpr bool value = false; };

template <bool, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { using type = T; };

template <class...> using expand_to_true = true_type;

template <class... Pred>
expand_to_true<typename enable_if<Pred::value>::type...> and_helper(int);

template <class...>
false_type and_helper(...);

template <class... Pred>
using And = decltype(and_helper<Pred...>(0));

static_assert(And<true_type, true_type>::value);
static_assert(!And<true_type, false_type>::value);
static_assert(And<>::value);

int main() {
    return (And<true_type, true_type>::value && !And<true_type, false_type>::value) ? 0 : 1;
}
