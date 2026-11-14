namespace std {
template <class T> T &&declval() noexcept;
template <class T, class U> struct is_same { static constexpr bool value = false; };
template <class T> struct is_same<T, T> { static constexpr bool value = true; };
template <class T> struct remove_reference { typedef T type; };
template <class T> struct remove_reference<T &> { typedef T type; };
template <class T> struct remove_reference<T &&> { typedef T type; };
template <class T> constexpr T &&forward(typename remove_reference<T>::type &t) noexcept { return static_cast<T &&>(t); }
template <bool B, class T = void> struct enable_if { };
template <class T> struct enable_if<true, T> { typedef T type; };
struct true_type { static constexpr bool value = true; };
struct false_type { static constexpr bool value = false; };
}
namespace nl {
namespace detail {
template <bool B, class T = void> using enable_if_t = typename std::enable_if<B, T>::type;
template <typename... Ts> struct make_void { using type = void; };
template <typename... Ts> using void_t = typename make_void<Ts...>::type;
template <typename T> struct static_const { static constexpr T value{}; };
template <typename J> void to_json(J &j, int v);
template <typename J> void to_json(J &j, double v);
struct to_json_fn {
	template <typename J, typename T>
	auto operator()(J &j, T &&val) const noexcept(noexcept(to_json(j, std::forward<T>(val))))
		-> decltype(to_json(j, std::forward<T>(val)), void());
};
}
namespace { constexpr const auto &to_json = detail::static_const<detail::to_json_fn>::value; }
template <typename T, typename = void> struct adl_serializer {
	template <typename J, typename U = T>
	static auto to_json(J &j, U &&val) noexcept(noexcept(::nl::to_json(j, std::forward<U>(val))))
		-> decltype(::nl::to_json(j, std::forward<U>(val)), void());
};
namespace detail {
template <typename T, typename... Args> using to_json_function = decltype(T::to_json(std::declval<Args>()...));
template <typename J, typename T, typename = void> struct has_to_json : std::false_type { };
template <typename J, typename T>
struct has_to_json<J, T, void_t<to_json_function<typename J::template ser<T, void>, J &, T>>> : std::true_type { };
}
template <template <typename, typename> class S = adl_serializer> struct basic {
	template <typename T, typename X> using ser = S<T, X>;
	using basic_t = basic;
	int kind;
	template <typename C, detail::enable_if_t<detail::has_to_json<basic_t, C>::value, int> = 0>
	basic(C &&val) noexcept(noexcept(S<C, void>::to_json(std::declval<basic_t &>(), std::forward<C>(val))));
	basic(const basic &);
};
using json = basic<>;
struct other { };
static_assert(detail::has_to_json<json, int>::value, "");
static_assert(detail::has_to_json<json, double>::value, "");
static_assert(!detail::has_to_json<json, other *>::value, "");
json mk(int v) { return json(v); }
static_assert(std::is_same<decltype(json(1)), json>::value, "");
}
