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
struct fn_t {
	template <typename J, typename T> auto operator()(J &j, T &&val) const noexcept(noexcept(conv(j, val))) -> decltype(conv(j, val), void());
};
constexpr fn_t fn{};
template <typename J, typename U> auto a1(J &j, U &&val) -> decltype(conv(j, val), void());
template <typename J, typename U> auto a2(J &j, U &&val) noexcept(noexcept(fn(j, val))) -> decltype(fn(j, val), void());
struct S3 { template <typename J, typename U> static auto a3(J &j, U &&val) noexcept(noexcept(::fn(j, val))) -> decltype(::fn(j, val), void()); };
template <class J, class T, class = void> struct h1 : std::false_type { };
template <class J, class T> struct h1<J, T, void_t<decltype(a1(std::declval<J &>(), std::declval<T>()))>> : std::true_type { };
template <class J, class T, class = void> struct h2 : std::false_type { };
template <class J, class T> struct h2<J, T, void_t<decltype(a2(std::declval<J &>(), std::declval<T>()))>> : std::true_type { };
template <class J, class T, class = void> struct h3 : std::false_type { };
template <class J, class T> struct h3<J, T, void_t<decltype(S3::a3(std::declval<J &>(), std::declval<T>()))>> : std::true_type { };
template <typename T, typename... Args> using a3_t = decltype(T::a3(std::declval<Args>()...));
template <class J, class T, class = void> struct h4 : std::false_type { };
template <class J, class T> struct h4<J, T, void_t<a3_t<S3, J &, T>>> : std::true_type { };
static_assert(h1<J0, int>::value, "");
static_assert(!h1<J0, other *>::value, "");
static_assert(h2<J0, int>::value, "");
static_assert(!h2<J0, other *>::value, "");
static_assert(h3<J0, int>::value, "");
static_assert(!h3<J0, other *>::value, "");
static_assert(h4<J0, int>::value, "");
static_assert(!h4<J0, other *>::value, "");
