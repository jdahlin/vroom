template <class A, class B> struct same { static constexpr bool value = false; };
template <class A> struct same<A, A> { static constexpr bool value = true; };
template <class T> T &&dv() noexcept;
template <bool B, class T = void> struct enable_if { };
template <class T> struct enable_if<true, T> { typedef T type; };
template <bool B, class T = void> using enable_if_t = typename enable_if<B, T>::type;
template <class T> struct is_pointer { static constexpr bool value = false; };
template <class T> struct is_pointer<T *> { static constexpr bool value = true; };
template <unsigned N> struct tag : tag<N - 1> { };
template <> struct tag<0> { };
template <class T> struct ident { using type = T; };
template <class T> using uncvref = typename ident<T>::type;
struct J {
	template <class P, enable_if_t<is_pointer<P>::value, int> = 0> auto get_ptr() noexcept -> P;
	template <class P, enable_if_t<is_pointer<P>::value, int> = 0> auto get_ptr() const noexcept -> P;
	template <class V, enable_if_t<!is_pointer<V>::value, int> = 0> V get_impl(tag<0>) const;
	template <class V, enable_if_t<same<V, void>::value, int> = 0> long get_impl(tag<1>) const;
	template <class B, enable_if_t<same<B, J>::value, int> = 0> J get_impl(tag<3>) const;
	template <class P, enable_if_t<is_pointer<P>::value, int> = 0>
	constexpr auto get_impl(tag<4>) const noexcept -> decltype(dv<const J &>().template get_ptr<P>());
	template <class VCV, class V = uncvref<VCV>>
	auto get() const noexcept(noexcept(dv<const J &>().template get_impl<V>(tag<4>{})))
		-> decltype(dv<const J &>().template get_impl<V>(tag<4>{}));
	template <class P, enable_if_t<is_pointer<P>::value, int> = 0>
	auto get() noexcept -> decltype(dv<J &>().template get_ptr<P>());
};
static_assert(same<decltype(dv<J &>().get_impl<char>(tag<4>{})), char>::value, "4");
static_assert(same<decltype(dv<J &>().get_impl<J>(tag<4>{})), J>::value, "5");
static_assert(same<decltype(dv<J &>().get<int>()), int>::value, "1");
static_assert(same<decltype(dv<const J &>().get<long>()), long>::value, "2");
static_assert(same<decltype(dv<J &>().get<int *>()), int *>::value, "3");
static_assert(same<decltype(dv<const J &>().get<const int *>()), const int *>::value, "6");
struct J2 { template <class V> V g(int) const; template <class V> V h() const; int k() const; };
J2 j;
static_assert(same<decltype(j.k()), int>::value, "g2-0");
static_assert(same<decltype(j.g<char>(1)), char>::value, "g2-1");
static_assert(same<decltype(j.h<long>()), long>::value, "g2-2");
static_assert(same<decltype(dv<J2 &>().h<long>()), long>::value, "g2-3");
static_assert(same<decltype(dv<J2 &>().template h<long>()), long>::value, "g2-4");
