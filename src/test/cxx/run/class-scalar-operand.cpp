// std: c++20
// A class operand of a built-in operator through its conversion function (atomic<int> == 4, as libc++'s), member
// functions overloaded on volatile (operator T() const and const volatile), _Atomic(T), constexpr auto enums.

struct C { int v; operator int() const { return v; } };
template <class T> struct B { T v; operator T() const volatile noexcept { return v + 100; } operator T() const noexcept { return v; } };
template <class T> struct D : B<T> {};
struct S { int s() const volatile { return 2; } int s() const { return 3; } };
template <class T> struct atom {
	T v;
	atom(T x) : v(x) {}
	operator T() const volatile noexcept { return __atomic_load_n(&v, 5); }
	operator T() const noexcept { return __atomic_load_n(&v, 5); }
	T fetch_add(T x) noexcept { return __atomic_fetch_add(&v, x, 5); }
	T load() const noexcept { return __atomic_load_n(&v, 5); }
};
enum class E { a = 3, b };
constexpr auto eb = E::b;

int main()
{
	int r = 0, bit = 1;
	C c{4};
	D<int> d;
	S s;
	atom<int> at{5};
	_Atomic(int) ai = 7;

	d.v = 4;
	r |= !(c == 4) * bit, bit <<= 1;
	r |= (c + 1 != 5) * bit, bit <<= 1;
	r |= !(d == 4) * bit, bit <<= 1;
	int x = d;
	r |= (x != 4) * bit, bit <<= 1;
	r |= (s.s() != 3) * bit, bit <<= 1;
	at.fetch_add(2), at.fetch_add(-3);
	r |= !(at == 4) * bit, bit <<= 1;
	r |= (at.load() + at != 8) * bit, bit <<= 1;
	r |= (ai != 7) * bit, bit <<= 1;
	r |= (eb != E::b) * bit, bit <<= 1;
	return r;
}
