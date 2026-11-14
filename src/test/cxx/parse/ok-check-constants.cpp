template <class T, bool = true> struct lim {
	typedef T type;
	static constexpr const bool is_signed = type(-1) < type(0);
	static constexpr const int digits = static_cast<int>(sizeof(type) * 8 - is_signed);
	static constexpr const type mn = is_signed ? type(type(1) << digits) : type(0);
	static constexpr const type mx = is_signed ? type(type(~0) ^ mn) : type(~0);
	static constexpr type max() noexcept { return mx; }
};
template <class T> struct nl : private lim<T> {
	typedef lim<T> base;
	static constexpr const bool is_signed = base::is_signed;
	static constexpr const int digits = base::digits;
};
static_assert(lim<long>::is_signed, "1");
static_assert(!lim<unsigned>::is_signed, "2");
static_assert(nl<int>::is_signed == nl<long>::is_signed, "3");
static_assert(nl<int>::digits == 31 && nl<unsigned char>::digits == 8, "4");
static_assert(int(-1) < int(0) && !(unsigned(-1) < unsigned(0)) && (unsigned char)(300) == 44 && char(-1) < 0, "5");
static_assert(lim<int>::mx == 2147483647 && lim<unsigned char>::mx == 255, "6");
namespace d2 { template <class C> auto begin(C &c) -> decltype(c.begin()); template <class T, unsigned long N> T *begin(T (&a)[N]); }
namespace d3 { using d2::begin; template <class T> T &&dv(); template <typename... T> using rb = decltype(begin(dv<T>()...)); }
struct V { int *begin(); };
template <class A, class B> struct same { static constexpr bool value = false; };
template <class A> struct same<A, A> { static constexpr bool value = true; };
static_assert(same<d3::rb<V &>, int *>::value, "7");
static_assert(same<d3::rb<int (&)[3]>, int *>::value, "8");
static_assert(!lim<unsigned long>::is_signed && !lim<unsigned long long>::is_signed && lim<long long>::is_signed, "9");
static_assert(-1 < 1 && !(-1 < 1u) && !(-1L < 1ul) && -1L < 1u && (unsigned long)(-1) > 5, "10");
static_assert(lim<unsigned long>::digits == 64 && lim<short>::digits == 15, "11");
