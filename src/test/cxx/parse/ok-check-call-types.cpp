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

// overload ranking: each argument's conversion, then a non-template over a template
char  ov(int);
short ov(double);
long  ov(const char *);
template<class T> long long ov(T *);
struct Base { }; struct Derived : Base { };
char  bd(Base &);
short bd(...);
template<class T> char  pk(T);
short pk(int);
void h(Derived d, float f, int *ip, char c)
{
	static_assert(sizeof(ov(1)) == 1, "");		// exact
	static_assert(sizeof(ov(1.5)) == 2, "");	// exact
	static_assert(sizeof(ov("s")) == 8, "");	// array to pointer, to const
	static_assert(sizeof(pk(1)) == 2, "");		// a non-template over a template, both exact
	static_assert(sizeof(pk(c)) == 1, "");		// the template is exact, the non-template converts
	static_assert(sizeof(bd(d)) == 1, "");		// derived to base over ...
}

// nullptr has a type of its own
typedef decltype(nullptr) np_t;
char  np(int *);
short np(np_t);
long  np(long);
struct NJ { NJ(int v) {} NJ(np_t = nullptr) {} int n; };
void hn(int *ip)
{
	NJ a;
	NJ b = nullptr;
	static_assert(sizeof(np(nullptr)) == 2 && sizeof(np(ip)) == 1 && sizeof(np(1L)) == 8, "");
	static_assert(sizeof(a.n) == 4, "");
}

// reference binding: the less const-qualified reference for an object that is not const, T&& for an rvalue
struct RB { };
template<class C> char  rb(C &);
template<class C> short rb(const C &);
char  rv(RB &&);
long  rv(const RB &);
RB make();
void hr(RB o, const RB co)
{
	static_assert(sizeof(rb(o)) == 1 && sizeof(rb(co)) == 2, "");
	static_assert(sizeof(rv(make())) == 1 && sizeof(rv(o)) == 8, "");
}
