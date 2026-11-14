namespace std {
template <class T> T &&declval() noexcept;
struct true_type { static constexpr bool value = true; };
struct false_type { static constexpr bool value = false; };
}
template <typename... Ts> struct make_void { using type = void; };
template <typename... Ts> using void_t = typename make_void<Ts...>::type;
struct J0 { };
struct other { };
template <typename J> void conv(J &j, int v);
template <typename J, typename U> auto a0(J &j, U &&val) -> decltype(conv(j, val));
template <typename J, typename U> auto a1(J &j, U &&val) -> decltype(conv(j, val), void());
template <class J, class T, class = void> struct h0 : std::false_type { };
template <class J, class T> struct h0<J, T, void_t<decltype(a0(std::declval<J &>(), std::declval<T>()))>> : std::true_type { };
template <class J, class T, class = void> struct h1 : std::false_type { };
template <class J, class T> struct h1<J, T, void_t<decltype(a1(std::declval<J &>(), std::declval<T>()))>> : std::true_type { };
template <class J, class T, class = void> struct h5 : std::false_type { };
template <class J, class T> struct h5<J, T, void_t<decltype(conv(std::declval<J &>(), std::declval<T>()), void())>> : std::true_type { };
static_assert(!h0<J0, other *>::value, "0");
static_assert(!h5<J0, other *>::value, "5");
static_assert(!h1<J0, other *>::value, "1");
