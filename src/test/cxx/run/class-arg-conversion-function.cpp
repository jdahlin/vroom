// a class argument for a by-value parameter of another class: the argument's conversion function (json's
// binary_writer<uint8_t>(o) with o an output_adapter, the parameter an output_adapter_t); its temporary is
// destroyed (checked after the block: vcc destroys temporaries at its end, clang at the full expression's)
struct Q { int *p; Q(int *x) : p(x) {} Q(const Q &o) : p(o.p) { ++*p; } ~Q() { --*p; } };
struct A { int *p; operator Q() const { return Q(p); } };
struct W { int v; explicit W(Q q) : v(*q.p) {} int get() const { return v; } };
int f(Q q) { return *q.p; }
int main()
{
	int n = 10, r, s;
	{
		A a{&n};
		r = W(a).get();
		s = f(a);
	}
	return r == 10 && (s == 10 || s == 9) && n == 8 ? 0 : 1;
}
