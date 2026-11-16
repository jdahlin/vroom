// p == n, n != p, n == n of a std::nullptr_t object (doctest's CHECK(p == nullptr): the nullptr forwarded as R&&)
typedef decltype(nullptr) nullptr_t;
template <class L> struct E {
	L lhs;
	template <class R> bool eq(R &&rhs) { bool res = static_cast<L &&>(lhs) == static_cast<R &&>(rhs); return res; }
	template <class R> bool ne(R &&rhs) { return rhs != lhs; }
};
int main()
{
	int x = 1;
	nullptr_t n = nullptr, m = nullptr;
	E<int *> a{&x};
	E<const int *> b{nullptr};
	return !a.eq(nullptr) && b.eq(nullptr) && a.ne(nullptr) && !b.ne(n) && n == m && !(n != m) ? 0 : 1;
}
