/* [abi:mangle.template-arg] a type-constraint whose concept is declared in
   an inline namespace, named through the enclosing one (std::integral in
   libc++'s std::__1; OpenTTD's PoolID operators) */
/* cxx-run-flags: -std=c++20 */
namespace std
{
inline namespace __1
{
template <class T> concept integral = __is_integral(T);
template <class B, class D>
inline constexpr bool is_base_of_v = __is_base_of(B, D);
template <class T> inline constexpr bool is_enum_v = __is_enum(T);
} // namespace __1
} // namespace std

namespace N
{
inline namespace v1
{
template <class T> concept integral = __is_integral(T);
}
} // namespace N

struct Base {};
template <class B> struct Id : Base {
    B value;
    constexpr auto operator+(const std::integral auto &val) const
    {
        return this->value + val;
    }
    constexpr auto operator%(const std::integral auto &val) const
    {
        return this->value % val;
    }
    constexpr auto f(const N::integral auto &val) const { return value - val; }
};
template <class T>
    requires std::is_base_of_v<Base, T>
constexpr auto operator+(const std::integral auto &val, const T &p)
{
    return p + val;
}
template <class Te, class Tp>
    requires std::is_enum_v<Te> && std::is_base_of_v<Base, Tp>
constexpr auto operator+(const Te &val, const Tp &p)
{
    return p + static_cast<int>(val);
}
constexpr auto h(const N::integral auto &val) { return val; }
enum E { A = 1 };

Id<int> id{{}, 20};

int main()
{
    unsigned u = 3;
    unsigned sum = (id + u) + (id % 3) + (3 + id) + (A + id) + id.f(u) + h(1);
    return static_cast<int>(sum) - (23 + 2 + 23 + 21 + 17 + 1);
}
