// The size of each call's type: vcc must evaluate every static_assert as true (VCC_CXX_STATIC_ASSERT=1,
// set by bench/cxx-parse-tests.py for ok-check-*), and clang accepts the file.
namespace n {
struct S { char tag[3]; short get(); long get() const; int *begin(); };
long long q(int);
double adl(S &);
template<class C> auto begin(C &c) -> decltype(c.begin()) { return c.begin(); }
}
template<class T> T same(T x) { return x; }
template<class T> auto twice(T x) { return x + x; }
template<class T, class U> T conv(U u) { return T(u); }
struct L { template<class T, class U> auto operator()(T &&a, U &&b) const -> decltype(a < b) { return a < b; } };
void g(n::S s, const n::S cs, L less, short h)
{
	static_assert(sizeof(n::q(1)) == 8, "");			// a qualified name
	static_assert(sizeof(s.get()) == 2, "");		// f() on an object that is not const
	static_assert(sizeof(cs.get()) == 8, "");		// f() const
	static_assert(sizeof(adl(s)) == 8, "");		// argument-dependent lookup
	static_assert(sizeof(same(h)) == 2, "");		// deduction
	static_assert(sizeof(twice(h)) == 4, "");		// an auto return type: short + short is int
	static_assert(sizeof(conv<char>(h)) == 1, "");	// an explicit template argument
	static_assert(sizeof(less(h, h)) == 1, "");		// operator() with a trailing return type over its parameters
	auto l = [](int x) { return x + 1L; };
	static_assert(sizeof(l(1)) == 8, "");		// a lambda
	auto p = &s;
	static_assert(sizeof(p->tag) == 3, "");		// auto, a pointer
	static_assert(sizeof(*begin(s)) == 4, "");	// ADL finds a template
}
