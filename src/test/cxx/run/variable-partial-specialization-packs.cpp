// [temp.spec.partial]: partial specializations of variable templates with
// packs: a primary ending in a pack, a specialization's pack after a
// detection argument (libc++'s __is_invocable_impl)
template <class...> using void_t = void;
int f(int, int);
template <class... A> using result_t = decltype(f(A()...));
template <class V, class... A> inline const bool impl = false;
template <class... A> inline const bool impl<void_t<result_t<A...> >, A...> = true;
template <class... A> inline const bool is_v = impl<void, A...>;
static_assert(is_v<int, int>, "");
static_assert(!is_v<int>, "");
static_assert(!is_v<int, int, int>, "");

template <class T, class... R> constexpr int count = 1 + count<R...>;
template <class T> constexpr int count<T> = 1;
static_assert(count<int, char, long> == 3, "");

template <class... A> constexpr int first_int = 0;
template <class... R> constexpr int first_int<int, R...> = 1 + sizeof...(R);
static_assert(first_int<int, char, char> == 3, "");
static_assert(first_int<char, int> == 0, "");
int main() { return is_v<int, int> && !is_v<int> && count<int, int> == 2 ? 0 : 1; }
