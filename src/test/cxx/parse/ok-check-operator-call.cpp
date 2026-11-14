template <class A, class B> struct same { static constexpr bool value = false; };
template <class A> struct same<A, A> { static constexpr bool value = true; };
template <class T> T &&dv() noexcept;
namespace n { struct S { }; struct O { O &operator<<(int); }; O &operator<<(O &, const S &); bool operator==(const S &, const S &); struct U { }; }
namespace m {
template <class...> using vt = void;
template <class T, class = void> struct ins { static constexpr bool value = false; };
template <class T> struct ins<T, decltype(operator<<(dv<n::O &>(), dv<const T &>()), void())> { static constexpr bool value = true; };
}
static_assert(same<decltype(operator<<(dv<n::O &>(), dv<const n::S &>())), n::O &>::value, "1");
static_assert(same<decltype(operator==(n::S(), n::S())), bool>::value, "2");
static_assert(m::ins<n::S>::value, "3");
static_assert(!m::ins<n::U>::value, "4");
static_assert(!m::ins<int>::value, "5");
