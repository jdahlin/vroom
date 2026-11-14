struct P { static constexpr auto value = 3 > 2; static constexpr auto n = 7; };
template <class T> struct Q { static constexpr auto value = sizeof(T) == 4; static constexpr auto n = sizeof(T) + 1; };
static_assert(P::value && P::n == 7, "1");
static_assert(Q<int>::value, "2");
static_assert(!Q<char>::value && Q<char>::n == 2, "3");
template <class A, class B> struct same { static constexpr bool value = false; };
template <class A> struct same<A, A> { static constexpr bool value = true; };
static_assert(same<decltype(P::n), const int>::value && same<decltype(Q<int>::value), const bool>::value, "4");
