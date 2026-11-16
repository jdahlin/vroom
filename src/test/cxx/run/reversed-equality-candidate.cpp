// std: c++20
// a == b, a != b with only b's member operator==: the reversed candidate b == a, !(b == a) (C++20
// [over.match.oper]/3.4; json's nullptr == j, 3 == j of its member operator==(ScalarType))
typedef decltype(nullptr) nullptr_t;
static int val(nullptr_t) { return 0; }
static int val(int i) { return i; }
struct J {
	int v;
	bool operator==(const J &o) const { return v == o.v; }
	template <class S> bool operator==(S s) const { return v == val(s); }
};
int main()
{
	J z{0}, t{3};
	nullptr_t n = nullptr;
	return (nullptr == z) && !(nullptr == t) && (3 == t) && (n != t) && !(n != z) && (z == n) ? 0 : 1;
}
