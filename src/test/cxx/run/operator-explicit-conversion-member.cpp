// std: c++20
// [over.match.oper]/3.2-3.3, [over.built]: an explicit conversion function is no implicit
// conversion of a built-in candidate's parameter, so a class whose only conversion function is
// explicit has no built-in candidates for == (the operator function wins); ++ of a class lvalue
// finds the member operator++ although the class converts to unsigned (a built-in ++ takes
// L&, which a class does not bind to); the non-member lookup of an operator inside a member
// function ignores the class's own operator members ([over.match.oper]/3.2)
struct ptr {
	void *p = nullptr;
	explicit operator bool() const { return p != nullptr; }
	friend bool operator==(const ptr &a, const ptr &b) { return a.p == b.p; }
	friend bool operator!=(const ptr &a, const ptr &b) { return !(a == b); }
};
struct day {
	unsigned char d;
	constexpr day &operator++() { ++d; return *this; }
	constexpr day operator++(int) { day t = *this; ++(*this); return t; }
	constexpr day &operator+=(int n) { d += n; return *this; }
	explicit constexpr operator unsigned() const { return d; }
};
struct num {
	int v;
	num &operator++() { v += 10; return *this; }
	operator int() const { return v; }
};
int main()
{
	int x = 0;
	ptr a, b{&x};
	if (a == b || !(a != b) || !(a == a))
		return 1;
	day d{250};
	++d;
	d++;
	d += 2;
	if (static_cast<unsigned>(d) != 253)
		return 2;
	num n{1};
	++n;
	if (n.v != 11)
		return 3;
	return 0;
}
