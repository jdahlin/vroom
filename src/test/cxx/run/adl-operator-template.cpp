// std: c++20
// an operator function template found by argument-dependent lookup alone ([over.match.oper]/3.2,
// [basic.lookup.argdep]/3), from another namespace and through a using-directive of the class's
// namespace (which ordinary lookup of operators does not find: the operands decide), alongside
// a non-template operator of the same namespace
namespace ns {
	template <class R> struct D { R r; R count() const { return r; } };
	template <class R1, class R2>
	D<long> operator-(const D<R1> &a, const D<R2> &b) { return D<long>{a.r - b.r}; }
	template <class R1, class R2>
	bool operator<(const D<R1> &a, const D<R2> &b) { return a.r < b.r; }
	D<int> operator+(const D<int> &a, const D<int> &b) { return D<int>{a.r + b.r}; }
	template <class R> D<R> operator+(const D<R> &a, R b) { return D<R>{a.r + b}; }
}
namespace other {
	long diff()
	{
		using namespace ns;
		D<int> a{7};
		D<long> b{2};
		return (a - b).count();
	}
}
int main()
{
	ns::D<int> a{7};
	ns::D<long> b{2};
	if ((a - b).count() != 5 || !(b < a) || a < b)
		return 1;
	if ((a + a).count() != 14 || (a + 3).count() != 10)
		return 2;
	if (other::diff() != 5)
		return 3;
	return 0;
}
