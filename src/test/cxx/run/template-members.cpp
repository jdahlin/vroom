// members of class template instances defined outside the class template (weak, as clang's), a nested
// class's operator() called on a temporary (vector's __destroy_vector(*this)()), a reference to a class
// declared but not yet defined, mutable members of a const object
static int hits;
template <class T> struct L {
	using size_type = unsigned long;
	size_type n = 3;
	mutable int calls = 0;
	size_type size() const noexcept;
	int other() const;
	class D { L &l; public: D(L &x) : l(x) {} void operator()() { hits += (int)l.n; } };
	~L() { D(*this)(); }
};
template <class T> constexpr typename L<T>::size_type L<T>::size() const noexcept { calls++; return n; }
template <class T> int L<T>::other() const { return calls; }
struct V;
struct R { V &v; R(V &x) : v(x) {} };
struct V { int k = 7; };
int main()
{
	{
		const L<int> l;
		if (l.size() != 3 || l.size() != 3 || l.other() != 2)
			return 1;
	}
	V v;
	R r(v);
	return hits == 3 && r.v.k == 7 ? 0 : 2;
}
