// __builtin_common_type<Tmpl, TypeIdentity, Empty, Ts...> as a base (libc++'s std::common_type): the decayed
// types, equal or arithmetic (usual arithmetic conversions); none: Empty (json's value_in_range_of)
template <class T> struct ti { using type = T; };
struct empty {};
template <class... A> struct common;
template <class... A> using common_t = typename common<A...>::type;
template <class... A> struct common : __builtin_common_type<common_t, ti, empty, A...> {};
template <class A, class T> long cast(T v) { using C = typename common<A, T>::type; return (long)static_cast<C>(v); }
template <class T, class U> struct same { static const bool value = false; };
template <class T> struct same<T, T> { static const bool value = true; };
struct S {};
template <class T> char has(typename T::type *);
template <class T> long has(...);
int main()
{
	bool ok = same<common<unsigned long, long>::type, unsigned long>::value && same<common<int, char>::type, int>::value &&
		  same<common<const int &, int>::type, int>::value && same<common<S, S>::type, S>::value &&
		  same<common<int, long, unsigned char>::type, long>::value && sizeof(has<common<S, int>>(0)) == sizeof(long) &&
		  cast<unsigned long>(-1L) == -1L;
	return ok ? 0 : 1;
}
