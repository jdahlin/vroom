// x = y of a class with operator=(const T&) and operator=(T&&): an lvalue y (a variable, *p of a const T*, a
// reference) takes the copy assignment; std::string's move assignment emptied json's strings (j.get<T>())
struct S { int k = 0; S &operator=(const S &) { k = 1; return *this; } S &operator=(S &&) { k = 2; return *this; } };
S mk() { return S(); }
int main()
{
	S a, b, c2, d, e, f;
	const S *p = &a;
	S *q = &a;
	const S &r = a;
	b = *p;
	c2 = *q;
	d = r;
	e = mk();
	f = static_cast<S &&>(a);
	return b.k == 1 && c2.k == 1 && d.k == 1 && e.k == 2 && f.k == 2 ? 0 : 1;
}
