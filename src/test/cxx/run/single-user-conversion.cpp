// std: c++20
// one user-defined conversion at most ([over.best.ics]/4): a converting constructor's class parameter takes no
// second one (int * is no It<const T> through It<const T>'s explicit constructor), so is_convertible says no and
// json's insert(pos, 5) is the (pos, json&&) overload, not (first, last) of iterators; an initializer_list
// parameter is never made from a non-braced argument
template <class T> struct It {
	T *p = nullptr;
	It() = default;
	It(It &&) noexcept = default;
	explicit It(T *q) noexcept : p(q) {}
	It(const It<const T> &o) noexcept : p(o.p) {}
};
template <class T> struct It<const T> { const T *p = nullptr; It() = default; explicit It(const T *q) noexcept : p(q) {} };
struct J {
	int v;
	J(int x) : v(x) {}
	int insert(It<const int>, const J &) { return 1; }
	int insert(It<const int>, J &&) { return 2; }
	int insert(It<const int>, It<const int>) { return 3; }
};
static_assert(!__is_convertible(int *, It<int>) && !__is_convertible(int, It<const int>));
int main() { J j(0); return j.insert(It<const int>(), 5) == 2 ? 0 : 1; }
