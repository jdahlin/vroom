// __is_nothrow_constructible and friends for classes: a constructor's noexcept (noexcept(false), throw()),
// implicit and = default constructors throwing nothing when their bases' and members' do not
struct A { A(int) noexcept {} A(const A &) {} };
struct B { int x; };
struct C { C() noexcept(false) {} };
struct M { M() {} M(const M &) noexcept {} };
struct N { M m; int k; };
struct P { A a; };
struct Q { Q() throw() {} };
static_assert(__is_nothrow_constructible(A, int));
static_assert(!__is_nothrow_constructible(A, const A &));
static_assert(__is_nothrow_constructible(B, const B &));
static_assert(!__is_nothrow_constructible(C));
static_assert(__is_nothrow_constructible(N, const N &));
static_assert(!__is_nothrow_constructible(P, const P &));
static_assert(__is_nothrow_constructible(Q));
int main()
{
	return __is_nothrow_constructible(N, const N &) && !__is_nothrow_constructible(C) ? 0 : 1;
}
