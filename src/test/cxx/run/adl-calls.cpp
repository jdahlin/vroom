// std: c++20
// calls found by argument-dependent lookup (a class's namespace, an enumeration's, a hidden friend;
// a local using-declaration of a deleted decoy), an enumerator converted to a class by a constructor
// template for enumerations (error_code == errc), a concept's default template argument
namespace ns {
	struct E { int v; friend int twice(E e) { return 2 * e.v; } };
	int mk(E e) { return e.v; }
	enum class K { a = 3, b = 4 };
	int mk(K k) { return (int)k; }
	struct Cond {
		int v;
		template <class T, int = sizeof(T)> Cond(T t) : v((int)t + 100) {}
	};
	bool operator==(const E &e, const Cond &c) { return e.v + 100 == c.v; }
}
namespace decoy { void mk() = delete; }
template <class T> int viaUsing(T t) { using decoy::mk; return mk(t); }
template <class T> int plain(T t) { return mk(t); }

template <class T, class U = int> concept same2 = __is_same(T, U);
template <class T> int only_int(T t) { if constexpr (same2<T>) return t; else return -1; }

int main()
{
	ns::E e{5};
	if (mk(e) != 5 || twice(e) != 10 || plain(e) != 5 || viaUsing(e) != 5)
		return 1;
	if (plain(ns::K::b) != 4 || viaUsing(ns::K::a) != 3)
		return 2;
	ns::E f{3};
	if (!(f == ns::K::a) || f == ns::K::b)
		return 3;
	if (only_int(7) != 7 || only_int(7L) != -1)
		return 4;
	return 0;
}
