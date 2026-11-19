// an explicit specialization declared after its class was named (std::char_traits<char> through
// string_view's default argument), static_cast to a class, T(x) through x's conversion function
// to T (no copy)
typedef unsigned long size_t;
template <class C> struct Tr;
template <class C, class T = Tr<C>> struct SV;
typedef SV<char> sv;
template <> struct Tr<char> {
	typedef char char_type;
	static int compare(const char_type *a, const char_type *b, size_t n) { return __builtin_memcmp(a, b, n); }
};
template <class C, class T> struct SV {
	const C *d;
	size_t n;
	int cmp(SV o) const { return T::compare(d, o.d, n < o.n ? n : o.n); }
};

class Ord {
	int v_;
public:
	explicit Ord(int v) : v_(v) {}
	int value() const { return v_; }
};
Ord order(int a) { return Ord(a < 0 ? -1 : a > 0); }
int signs(int a) { Ord o = order(a); return static_cast<Ord>(o).value() + static_cast<Ord>(order(a)).value() + static_cast<Ord>(a).value(); }

int copies;
struct V {
	const char *p;
	V(const char *x) : p(x) {}
	V(const V &o) : p(o.p) { copies++; }
};
struct S {
	const char *p;
	operator V() const { return V(p); }
};
const char *via(const S &s) { return V(s).p; }

int main()
{
	sv a{"abc", 3}, b{"abd", 3};
	if (a.cmp(b) >= 0 || b.cmp(a) <= 0 || a.cmp(a))
		return 1;
	if (signs(5) != 7 || signs(-2) != -4 || signs(0) != 0)
		return 2;
	S s{"x"};
	if (via(s) != s.p || copies)
		return 3;
	return 0;
}
