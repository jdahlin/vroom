// std: c++20
// [expr.prim.id.qual]/2, [temp.concept]/3: namespace-qualified variable
// template-ids and concept-ids, including a concept with a pack parameter.
namespace meta {
template <class T> inline constexpr int width_v = sizeof(T);
template <class... Ts> inline constexpr int count_v = sizeof...(Ts);
template <class T, class... Rest>
concept Sized = (sizeof(T) >= 4) && ((sizeof(Rest) >= 1) && ...);
}

static_assert(meta::Sized<int>);
static_assert(meta::Sized<int, char, short>);
static_assert(!meta::Sized<char>);
static_assert(meta::width_v<long> == 8);
static_assert(meta::count_v<int, char> == 2);

template <class T> constexpr int total(T x) {
    return x + meta::width_v<T> + meta::count_v<T, char>;
}

int main() {
    return total(3) != 9;
}
