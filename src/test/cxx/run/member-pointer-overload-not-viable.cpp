// a function template whose parameter is a pointer to member (R C::* p) is not viable for an argument of class or
// arithmetic type (libc++'s __function::__is_null(__f) for a lambda, std::function from a lambda)
template <class F> bool is_null(const F &) { return false; }
template <class F> bool is_null(F *p) { return !p; }
template <class R, class C> bool is_null(R C::*p) { return !p; }
struct S { int m; };
int main()
{
	auto l = [](int x) { return x + 1; };
	int k = 0, *np = nullptr;
	return !is_null(l) && !is_null(k) && is_null(np) && !is_null(S{1}) ? 0 : 1;
}
