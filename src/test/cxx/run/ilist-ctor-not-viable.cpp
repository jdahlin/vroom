// std: c++20
// T{a, b} takes an initializer_list<E> constructor only when the elements convert to E: SkSpan<SkPoint>
// {ptr, 4} (a pointer is no SkPoint, an aggregate) is the (pointer, size) constructor
namespace std {
template <class E> class initializer_list {
  const E *__begin_;
  decltype(sizeof 0) __size_;
public:
  constexpr initializer_list() noexcept : __begin_(nullptr), __size_(0) {}
  constexpr decltype(sizeof 0) size() const noexcept { return __size_; }
  constexpr const E *begin() const noexcept { return __begin_; }
};
}
struct P { float x, y; static P Zero() { return P{}; } }; /* (P{}: the default constructor code generation makes) */
template <typename T> struct Span {
	T *p;
	unsigned long n;
	int which;
	Span(T *q, int m) : p(q), n(m), which(1) {}
	Span(std::initializer_list<T> il) : p((T *)il.begin()), n(il.size()), which(2) {}
};
struct V {
	int which;
	V(std::initializer_list<int>) : which(2) {}
	V(int, int) : which(1) {}
};
static int use(Span<P> s) { return s.which * 10 + (int)s.n; }
int main()
{
	P q[4] = {};
	Span<P> a{q, 4};
	Span<P> b{{1, 2}, {3, 4}};
	V v{1, 2};
	if (a.which != 1 || a.n != 4 || a.p != q || b.which != 2 || b.n != 2 || b.p[1].y != 4)
		return 1;
	if (v.which != 2 || use({q, 3}) != 13)
		return 2;
	return 0;
}
