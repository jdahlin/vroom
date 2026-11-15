// T &operator=(T&&) = default and (const T&) = default of a trivially copyable class: its bytes copied, the
// object the result (clang defines no function for them: json's iter_impl)
template <class T> struct It {
	T *p; int k;
	It() = default;
	It(T *q, int n) : p(q), k(n) {}
	It(It &&) noexcept = default;
	It(const It &) = default;
	It &operator=(It &&) noexcept = default;
	It &operator=(const It &) = default;
};
struct E { E &operator=(const E &) = default; };
template <class T> It<T> mk(T *p, int n) { return It<T>(p, n); }
int main()
{
	int a[2];
	It<int> x(a, 1), y;
	y = mk(a + 1, 2);
	It<int> *py = &y;
	(*py = x).k += 5;
	E e1, e2;
	e1 = e2;
	return y.p == a && y.k == 6 && x.k == 1 ? 0 : 1;
}
