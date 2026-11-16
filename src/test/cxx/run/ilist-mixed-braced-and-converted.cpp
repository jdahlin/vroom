// T{{a, b}, x, "s"}: braced elements next to elements that need a converting constructor (json's
// {{"foo", 1}, "foo", "baz"}); checking that the elements convert must not lose which ones are braced
namespace std {
template <class E> class initializer_list {
	const E *__begin_; unsigned long __size_;
public:
	typedef E value_type;
	constexpr const E *begin() const { return __begin_; }
	constexpr unsigned long size() const { return __size_; }
};
}
struct S { const char *p; };
struct E {
	int a, b;
	E(int a) : a(a), b(0) {}
	E(int a, int b) : a(a), b(b) {}
	E(const char *s) : a(s[0]), b(-1) {}
	template <class T> E(const T *p, int n) : a(n), b(-2) {}
};
struct L {
	int sum, n;
	L(std::initializer_list<E> l) : sum(0), n((int)l.size()) { for (unsigned long i = 0; i < l.size(); i++) sum += l.begin()[i].a * 10 + l.begin()[i].b; }
};
int main()
{
	L l = { {2, 3}, 1, "a", {4, 5} };
	return l.n == 4 && l.sum == 23 + 10 + (97 * 10 - 1) + 45 ? 0 : 1;
}
