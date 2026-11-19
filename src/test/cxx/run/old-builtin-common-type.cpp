// clang's __builtin_common_type<Tmpl, TypeIdentity, Empty, Ts...>, as libc++'s common_type uses it (a base
// class): TypeIdentity<the common type> (one type after decay, arithmetic types' usual conversions, a pointer
// and nullptr_t, folded pairwise), or Empty when there is none
// std: c++20
template <class T> struct type_identity { using type = T; };
struct empty {};
template <class... Ts> struct common_type;
template <class... Ts> using common_type_t = typename common_type<Ts...>::type;
template <class... Ts> struct common_type : __builtin_common_type<common_type_t, type_identity, empty, Ts...> {};
template <class A, class B> struct same { static constexpr bool value = false; };
template <class A> struct same<A, A> { static constexpr bool value = true; };
template <class T, class = void> struct has_type { static constexpr bool value = false; };
template <class T> struct has_type<T, decltype((void)sizeof(typename T::type))> { static constexpr bool value = true; };
template <class O, class T> long widen(T v) { using C = typename common_type<O, T>::type; return static_cast<C>(v); }
static_assert(same<common_type<int, long>::type, long>::value, "");
static_assert(same<common_type<char, short, unsigned>::type, unsigned>::value, "");
static_assert(same<common_type<const int &>::type, int>::value, "");
static_assert(same<common_type<int *, decltype(nullptr)>::type, int *>::value, "");
static_assert(same<common_type<int[3], int *>::type, int *>::value, "");
static_assert(!has_type<common_type<int, void>>::value, "");
static_assert(!has_type<common_type<>>::value, "");
int main() { return widen<int>(3L) + widen<long>(4) == 7 ? 0 : 1; }
