// std: c++20
// the builtins of codegen/builtins.list: floating classification (long double too), the constants,
// integers (clz, ctz, bswap, overflow), the generic and _n atomics, fences, lock-free queries,
// launder, assume, expect; clang's library builtins (libbuiltins.inc); a constexpr enumeration constant
enum class order : int { relaxed, acquire = 2 };
inline constexpr order order_acquire = order::acquire;
int as_int(order o) { return (int)o; }

int main()
{
	int r = 0, bit = 1;
#define T(x) do { if (!(x)) r |= bit; bit <<= 1; } while (0)
	volatile long double ld = 1e-4940L, one = 1.0L;
	volatile double inf = __builtin_huge_val(), zero = 0.0;
	volatile float qnan = __builtin_nanf("");
	T(!__builtin_isnormal(ld) && __builtin_isnormal(one));
	T(__builtin_isinf(inf) && !__builtin_isfinite(inf) && __builtin_isfinite(zero));
	T(__builtin_isnan(qnan) && !__builtin_isnan(zero) && __builtin_isnan(__builtin_nanl("")));
	T(__builtin_signbit(-1.0) && !__builtin_signbit(2.0f) && __builtin_huge_vall() > 1e300L);
	T(__builtin_clz(1u) == 31 && __builtin_ctzll(8ull) == 3 && __builtin_popcountg(0xffu) == 8);
	T(__builtin_bswap32(0x11223344u) == 0x44332211u && __builtin_bswap16((unsigned short)0x1122) == 0x2211);
	int sum;
	T(__builtin_add_overflow(0x7fffffff, 1, &sum) && !__builtin_mul_overflow(6, 7, &sum) && sum == 42);
	long v = 5, old, nv = 9;
	T(__atomic_fetch_add(&v, 2, 5) == 5 && __atomic_load_n(&v, 5) == 7);
	__atomic_load(&v, &old, 5);
	__atomic_store(&v, &nv, 5);
	T(old == 7 && v == 9 && __atomic_compare_exchange(&v, &old, &nv, false, 5, 5) == false && old == 9);
	__atomic_thread_fence(5);
	T(__atomic_always_lock_free(sizeof(long), 0) && !__atomic_always_lock_free(32, 0));
	int x = 4, *p = __builtin_launder(&x);
	__builtin_assume(x == 4);
	T(*p == 4 && __builtin_expect(x, 4) == 4);
	T(__builtin_sqrt(16.0) == 4.0 && __builtin_ceill(2.5L) == 3.0L && __builtin_strlen("abc") == 3);
	T(as_int(order_acquire) == 2);
	return r;
}
