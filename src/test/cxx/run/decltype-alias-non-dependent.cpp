// Alias template with decltype(...) used in a non-type template parameter
// default argument (as in fmt's mapped_t / mapped_type_constant /
// stored_type_constant).

enum class kind { int_kind = 1, long_kind = 2, custom_kind = 3 };

template <typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

template <typename T, typename Char>
struct type_constant : integral_constant<kind, kind::custom_kind> {};

template <typename Char>
struct type_constant<int, Char> : integral_constant<kind, kind::int_kind> {};

template <typename Char>
struct type_constant<long, Char> : integral_constant<kind, kind::long_kind> {};

template <typename T>
T &declval();

namespace detail
{
template <typename Char>
struct mapper {
    static auto map(int) -> int;
    static auto map(long) -> long;
    static auto map(const void *) -> void;
};

template <typename T, typename Char>
using mapped_t = decltype(detail::mapper<Char>::map(declval<T &>()));

template <typename T, typename Char = char>
using mapped_type_constant = type_constant<mapped_t<T, Char>, Char>;

template <typename T, typename Context,
          kind K = mapped_type_constant<T, typename Context::char_type>::value>
using stored_type_constant =
    integral_constant<kind, Context::builtin_types || K == kind::int_kind
                                ? K
                                : kind::custom_kind>;
} // namespace detail

struct context_all {
    using char_type = char;
    static constexpr bool builtin_types = true;
};

struct context_int_only {
    using char_type = char;
    static constexpr bool builtin_types = false;
};

int main()
{
    if (detail::stored_type_constant<int, context_all>::value != kind::int_kind)
        return 1;
    if (detail::stored_type_constant<long, context_all>::value !=
        kind::long_kind)
        return 2;
    if (detail::stored_type_constant<const void *, context_all>::value !=
        kind::custom_kind)
        return 3;
    if (detail::stored_type_constant<int, context_int_only>::value !=
        kind::int_kind)
        return 4;
    if (detail::stored_type_constant<long, context_int_only>::value !=
        kind::custom_kind)
        return 5;
    return 0;
}
