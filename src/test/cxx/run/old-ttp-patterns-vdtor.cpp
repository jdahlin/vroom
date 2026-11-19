// std: c++20
// A partial specialization whose pattern uses a template template parameter as a type (A<T, X...>: libc++'s
// __allocator_traits_rebind), a missing T::template rb<U>::other as a substitution failure, a defaulted
// destructor that is trivial, and p->~T() of a virtual destructor through the vtable.
void *operator new(unsigned long, void *p) noexcept { return p; }
template <class T> struct Al {};
template <class T, class U, bool = false> struct R;
template <template <class, class...> class A, class T, class... X, class U> struct R<A<T, X...>, U, false> { using type = A<U, X...>; };
template <class X, class Y> struct same { static const bool v = false; };
template <class X> struct same<X, X> { static const bool v = true; };

template <class...> using vt = void;
struct NoRb {};
struct HasRb { template <class U> struct rb { using other = int; }; };
template <class T, class = void> struct has { static const bool v = false; };
template <class T> struct has<T, vt<typename T::template rb<int>::other>> { static const bool v = true; };

struct I { int *p; I(int *q) : p(q) {} I(const I &o) : p(o.p) {} ~I() = default; bool operator!=(const I &o) const { return p != o.p; } };
struct V { int a[3] = {1, 2, 3}; I begin() { return I(a); } I end() { return I(a + 3); } };

int n;
struct Base { virtual ~Base() { n += 1; } };
struct Derived : Base { ~Derived() { n += 10; } };

int main()
{
	int r = 0, bit = 1;

	r |= !same<R<Al<int>, char>::type, Al<char>>::v * bit, bit <<= 1;
	r |= (has<NoRb>::v || !has<HasRb>::v) * bit, bit <<= 1;
	V v;
	int s = 0;
	for (I it = v.begin(); it != v.end(); ++it.p)
		s += *it.p;
	r |= (s != 6) * bit, bit <<= 1;
	alignas(Derived) char buf[sizeof(Derived)];
	Base *b = new (buf) Derived;
	b->~Base();
	r |= (n != 11) * bit, bit <<= 1;
	return r;
}
