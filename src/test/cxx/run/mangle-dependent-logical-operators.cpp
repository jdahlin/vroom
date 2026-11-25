// [abi:expressions] ||, && and the other binary operators, true and false (L b 1 E), and a variable
// template's specialization in a value-dependent template argument (libc++'s swap of std::array:
// __enable_if_t<_Size == 0 || __is_swappable_v<_Tp>>); symbols = clang's

template <bool B> struct bc { static constexpr bool value = B; };
typedef bc<true> yes;
typedef bc<false> no;
template <bool C, class R = void> struct en {};
template <class R> struct en<true, R> { typedef R type; };
template <bool C, class R = void> using en_t = typename en<C, R>::type;
template <class A, class B> struct same : no {};
template <class A> struct same<A, A> : yes {};
template <class... T> struct And { static constexpr bool value = true; };
template <class T> struct holder { typedef T type; };
template <class T> constexpr bool is_v = true;

namespace na2_m {
template <class T, unsigned long N> struct arr { T e[N + 1]; };
template <class T, unsigned long N, en_t<is_v<T>, int> = 0> int sw(arr<T, N>&) { return 1; }
int run() { arr<int, 2> a; return sw(a) - 1; }
} // namespace na2_m

namespace na3_m {
template <class T, unsigned long N> struct arr { T e[N + 1]; };
template <class T, unsigned long N, en_t<N == 0 || is_v<T>, int> = 0> int sw(arr<T, N>&) { return 1; }
int run() { arr<int, 2> a; return sw(a) - 1; }
} // namespace na3_m

namespace na4_m {
template <class T, unsigned long N> struct arr { T e[N + 1]; };
template <class T, unsigned long N, en_t<N == 0 || true, int> = 0> int sw(arr<T, N>&) { return 1; }
int run() { arr<int, 2> a; return sw(a) - 1; }
} // namespace na4_m

namespace na5_m {
template <class T, unsigned long N> struct arr { T e[N + 1]; };
template <class T, unsigned long N, en_t<is_v<T> || N == 0, int> = 0> int sw(arr<T, N>&) { return 1; }
int run() { arr<int, 2> a; return sw(a) - 1; }
} // namespace na5_m

int main() {
  int failed = 0;
  failed += na2_m::run() != 0;
  failed += na3_m::run() != 0;
  failed += na4_m::run() != 0;
  failed += na5_m::run() != 0;
  return failed;
}
