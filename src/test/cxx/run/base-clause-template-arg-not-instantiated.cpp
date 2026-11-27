// [temp.inst]/2: a class template specialization named as a template-argument
// inside a base-clause (e.g. conditional_t<B, E<T>, F>) is only named, not
// implicitly instantiated, even though the base-clause itself requires a
// complete base class.

template <class T> T &&declval() noexcept;

template <class T, T v> struct integral_constant {
    static constexpr T value = v;
};
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template <bool B, class T, class F> struct conditional {
    using type = T;
};
template <class T, class F> struct conditional<false, T, F> {
    using type = F;
};
template <bool B, class T, class F>
using conditional_t = typename conditional<B, T, F>::type;

template <class...> using void_t = void;

namespace detail {
template <typename T, unsigned long N>
auto range_begin(const T (&arr)[N]) -> const T *
{
    return arr;
}

template <typename T>
auto range_begin(T &&rng) -> decltype(static_cast<T &&>(rng).begin())
{
    return static_cast<T &&>(rng).begin();
}

template <typename T>
auto range_begin(T &&rng) -> decltype(begin(static_cast<T &&>(rng)))
{
    return begin(static_cast<T &&>(rng));
}

template <typename T, typename Enable = void>
struct has_begin : false_type {};

template <typename T>
struct has_begin<T, void_t<decltype(*detail::range_begin(declval<T &>()))>>
    : true_type {};

template <typename Range>
using range_reference_type = decltype(*detail::range_begin(declval<Range &>()));

template <typename T>
struct range_kind
    : integral_constant<int, static_cast<int>(sizeof(range_reference_type<T>))> {};
} // namespace detail

template <typename T>
struct format_kind
    : conditional_t<detail::has_begin<T>::value, detail::range_kind<T>,
                    integral_constant<int, 0>> {};

struct non_range_with_begin_field {
    int begin;
};

struct my_range {
    int data[3];
    int *begin() { return data; }
};

int main()
{
    if (format_kind<int>::value != 0)
        return 1;
    if (format_kind<non_range_with_begin_field>::value != 0)
        return 2;
    if (format_kind<my_range>::value != static_cast<int>(sizeof(int)))
        return 3;
    return 0;
}
