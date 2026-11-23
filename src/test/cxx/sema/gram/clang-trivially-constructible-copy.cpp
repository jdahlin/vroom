// __is_trivially_constructible(T, T-argument) chooses the constructor by overload resolution ([over.match.ctor]):
// a user-declared X(X&) beside X(const X&) = default (CWG 683), the implicit copy constructor of a class derived from it;
// every value is clang++'s
struct A { A() = default; A(const A &) = default; A(A &); };
static_assert(__is_trivially_constructible(A, const A &), "");
static_assert(!__is_trivially_constructible(A, A &), "");
struct B : A {};
static_assert(__is_trivially_constructible(B, const B &), "");
static_assert(__is_trivially_constructible(B, B &), "");
struct M { A a; };
static_assert(__is_trivially_constructible(M, M &), "");
static_assert(__is_trivially_constructible(M, const M &), "");

int main()
{
	return 0;
}
