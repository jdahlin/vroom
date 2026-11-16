// a member template's partial specialization (here matching through a template template parameter) applies
// in each instance of the enclosing class template (json_pointer<basic_json<...>>'s string_t_helper)
template <class K, class V> struct Map {};
template <template <class, class> class O, class S> struct BJ {};
template <class T, class U> struct same { static constexpr bool v = false; };
template <class T> struct same<T, T> { static constexpr bool v = true; };
template <class R> struct JP {
	template <class T> struct helper { using type = T; };
	template <template <class, class> class O, class S> struct helper<BJ<O, S>> { using type = S; };
	using string_t = typename helper<R>::type;
};
template <class T> struct H2 { using type = T; };
template <template <class, class> class O, class S> struct H2<BJ<O, S>> { using type = S; };
int main() { return same<JP<BJ<Map, long>>::string_t, long>::v * 2 + same<H2<BJ<Map, long>>::type, long>::v == 3 ? 0 : 1; }
