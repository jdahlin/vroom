/* each function is defined, so that clang's object names it */
namespace n { struct S { }; struct T { struct In { }; }; namespace m { struct U { }; void deep(U, S) { } } void f(S) { } void g(S *, const S &, S &&) { } }
struct G { };
template <class A> struct V { };
template <class A, class B> struct P { };
namespace n { template <class A> struct W { struct Inner { }; }; }
void a() { }
void b(int, unsigned, long, unsigned long, long long, unsigned long long, short, unsigned short, char, signed char, unsigned char, bool, float, double, long double) { }
void c(int *, const int *, int **, const char *const *, int &, const int &, int &&) { }
void d(G, G *, const G &, G &, G) { }
void e(n::S, n::S, n::T::In, n::m::U, n::S *) { }
void f(V<int>, V<int> *, V<G>, V<n::S>, V<V<int>>) { }
void g(P<int, long>, P<G, G>, P<V<int>, n::S>, P<int, long> &) { }
void h(n::W<int>, n::W<n::S>, n::W<int>::Inner, n::W<n::W<int>>) { }
void i(int (*)(int), void (*)(G, G), int (&)(const char *), int[3], int[]) { }
void j(const G *const, volatile int *, const volatile G &, int *const *) { }
void k(int, ...) { }
namespace n { void h(m::U, T::In, T::In *, m::U &) { } int operator+(S, S) { return 0; } bool operator==(const S &, const S &) { return true; } S &operator<<(S &s, int) { return s; } void operator-(S) { } }
namespace { struct Anon { }; __attribute__((used)) void hidden(int) { } __attribute__((used)) void hidden2(Anon) { } }
namespace std { struct allocator_like { }; template <class T> struct vec { }; void sf(allocator_like, vec<int>, vec<allocator_like>) { } namespace inner { struct I { }; void sg(I, allocator_like, I) { } } }
void l(std::allocator_like, std::vec<int>, std::inner::I, std::vec<std::inner::I>) { }
void m(decltype(nullptr), void *, const void *) { }
