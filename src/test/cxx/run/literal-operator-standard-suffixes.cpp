/* [usrlit.suffix]/1: suffixes without a leading underscore are reserved
   for the standard library, which declares them (<chrono>, <string>);
   they are accepted, in an inline namespace, as clang does (warning only). */
namespace std
{
inline namespace literals
{
inline namespace chrono_literals
{
struct hours {
    long r;
    constexpr hours(long x) : r(x) {}
};
constexpr hours operator""h(unsigned long long v)
{
    return hours(static_cast<long>(v));
}
constexpr hours operator""min(unsigned long long v)
{
    return hours(static_cast<long>(v) * 100);
}
constexpr hours operator"" _x(unsigned long long v)
{
    return hours(static_cast<long>(v) * 10000);
}
} // namespace chrono_literals
} // namespace literals
} // namespace std

struct sv {
    const char *p;
    unsigned long n;
};
inline namespace L
{
constexpr sv operator""sv(const char *p, unsigned long n)
{
    return {p, n};
}
} // namespace L

using namespace std::literals;

int main()
{
    auto a = 3h;
    auto b = 4min;
    auto c = 1_x;
    volatile auto n = ("abc"sv).n;
    return a.r + b.r + c.r + static_cast<long>(n) == 10406 ? 0 : 1;
}
