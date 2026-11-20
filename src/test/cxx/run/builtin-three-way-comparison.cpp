// [expr.spaceship]: <=> of arithmetic operands is std::strong_ordering or std::partial_ordering, usable in a requirement
// std: c++20
namespace std {
struct strong_ordering {
    signed char v;
    static const strong_ordering less, equal, equivalent, greater;
};
inline constexpr strong_ordering strong_ordering::less{-1}, strong_ordering::equal{0}, strong_ordering::equivalent{0},
    strong_ordering::greater{1};
struct partial_ordering {
    signed char v;
    static const partial_ordering less, equivalent, greater, unordered;
};
inline constexpr partial_ordering partial_ordering::less{-1}, partial_ordering::equivalent{0},
    partial_ordering::greater{1}, partial_ordering::unordered{2};
}
template <class T, class U> concept same_as = __is_same(T, U);
template <class T> concept strong = requires(const T &a, const T &b) { { a <=> b } -> same_as<std::strong_ordering>; };
template <class T> concept partial = requires(const T &a, const T &b) { { a <=> b } -> same_as<std::partial_ordering>; };
template <class T> concept spaceship = requires(const T &a, const T &b) { a <=> b; };
struct E { };
enum class K { x };
static_assert(strong<int> && strong<unsigned long> && strong<char> && strong<int *> && strong<void *> && strong<K>);
static_assert(partial<double> && partial<float> && !strong<double>);
static_assert(!spaceship<E> && !spaceship<bool *&> == false && !spaceship<void>);
template <class T> int kind(const T &) requires strong<T> { return 1; }
template <class T> int kind(const T &) requires partial<T> { return 2; }
template <class T> int kind(const T &) requires (!spaceship<T>) { return 3; }
int main() { E e; return kind(1) == 1 && kind(1.0) == 2 && kind(e) == 3 ? 0 : 1; }
