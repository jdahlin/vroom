// std: c++20
// a partial specialization of a variable template named by a qualified template-id
// ([dcl.meaning]/1, [temp.spec.partial]), declared outside the namespace
namespace ns {
	template <class T> inline constexpr int rank = 0;
	template <class T, int N> inline constexpr int sized = N;
}
template <class T> inline constexpr int ns::rank<T *> = 1;
template <class T> inline constexpr int ns::sized<T[3], 3> = 30;
namespace ns {
	static_assert(rank<int> == 0);
	static_assert(rank<int *> == 1);
	static_assert(sized<int, 5> == 5);
	static_assert(sized<char[3], 3> == 30);
	int probe() { return rank<char> + 10 * rank<void *> + 100 * sized<long[3], 3>; }
}
int main()
{
	if (ns::probe() != 3010)
		return 1;
	return 0;
}
