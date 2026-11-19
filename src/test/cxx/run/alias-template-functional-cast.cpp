// std: c++20
// [expr.type.conv]/1 with [temp.alias]/2: an alias template-id as the simple-type-specifier of a
// functional cast; the type-generic bit counts of libc++'s numeric_limits::digits
template <class T> using unsigned_of = unsigned;
template <class T> struct widen { using type = unsigned long long; };
template <class T> using widen_t = typename widen<T>::type;
template <class T> struct box { int v; box(int x) : v(x) {} };
template <class T> using int_box = box<T>;

template <class T> struct limits {
	static constexpr int digits = __builtin_popcountg(static_cast<unsigned_of<T> >(~unsigned_of<T>(0)));
};

template <class T> int via_alias() { return (int)widen_t<T>(5) + (int)unsigned_of<T>{7}; }

int main()
{
	int r = 0, bit = 1;
#define T(x) do { if (!(x)) r |= bit; bit <<= 1; } while (0)
	static_assert(limits<int>::digits == 32);
	T(unsigned_of<char>(3) == 3u);
	T(int_box<int>(4).v == 4);
	T(via_alias<int>() == 12);
	T(__builtin_popcountg(0xf0f0u) == 8);
	T(__builtin_clzg(1ull) == 63);
	T(__builtin_ctzg(8u) == 3);
	static_assert(__builtin_popcountg(0xffull) == 8);
	static_assert(__builtin_clzg(1u) == 31 && __builtin_clzg(0u, 7) == 7 && __builtin_ctzg(8ull) == 3);
	return r;
}
