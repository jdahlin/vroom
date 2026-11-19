// std: c++20
// An enumerator is a prvalue (E&& and a forwarding reference bind it: vector<json>::emplace_back(value_t::array));
// a class with trivial copy, move, assignment and destructor is trivially copyable though it has a constructor.
enum class E : unsigned char { a, b };
enum P { p0, p1 };

int take(E &&e) { return e == E::b; }
template <class... A> int count(A &&...a) { return sizeof...(a); }
template <class... A> int fwd(A &&...a) { return count(static_cast<A &&>(a)...); }

struct J { E t; J(E e) : t(e) {} };
struct K { int x; K(int v) : x(v) {} K &operator=(const K &o) { x = o.x + 1; return *this; } };
struct L { K k; L() : k(1) {} };

int main()
{
	int r = 0, bit = 1;

	r |= (take(E::b) != 1) * bit, bit <<= 1;
	r |= (fwd(E::b, p1) != 2) * bit, bit <<= 1;
	r |= !__is_trivially_copyable(J) * bit, bit <<= 1;
	r |= __is_trivially_copyable(K) * bit, bit <<= 1;
	r |= __is_trivially_copyable(L) * bit, bit <<= 1;
	return r;
}
