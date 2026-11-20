// [dcl.fct.spec]/2: explicit(constant-expression) on a constructor template is
// evaluated: explicit(false) converts implicitly, a value-dependent condition
// is decided per specialization.  [except.spec], [class.mem]/6: a member's
// noexcept-specifier names a member alias declared before it and a ::value.
template <bool B> struct Const { static constexpr bool value = B; };
struct S {
	template <class T = int>
	explicit(false) S(T v = 1) : x(v) {}
	int x;
};
template <class T> struct Box {
	using value_type = T;
	T v;
	Box(T a) : v(a) {}
	Box &operator=(const Box &o) noexcept(Const<true>::value &&
	                                       Const<sizeof(value_type) != 0>::value)
	{
		v = o.v;
		return *this;
	}
};
int take(S s) { return s.x; }
int main()
{
	Box<int> a(1), b(2);
	a = b;
	S s = 5;
	return take(7) + s.x + a.v - 14;
}
