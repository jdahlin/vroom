// a value template argument of 2^63 and up (libc++'s __non_unique_rtti_bit: 1ULL << 63), mangled unsigned
template <class T, T v> struct ic { static const T value = v; };
template <unsigned long long v> struct u { static unsigned long long f() { return v; } };
struct S {
	static bool unique(unsigned long x) { return !(x & bit::value); }
private:
	typedef ic<unsigned long, (1ULL << 63)> bit;
};
int main() {
	if (!S::unique(1) || S::unique(1ULL << 63))
		return 1;
	return u<(1ULL << 63)>::f() + u<~0ULL>::f() == (1ULL << 63) - 1 ? 0 : 2;
}
