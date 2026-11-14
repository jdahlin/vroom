/* base classes: layout offsets, members and member functions through a derived object, constructors and destructors chained */
int log_[32]; int nlog = 0;
struct Base { int id; Base(int i) : id(i) { log_[nlog++] = 1; } ~Base() { log_[nlog++] = -1; } int get() const { return id; } void set(int v) { id = v; } };
struct Mid : Base { int m; Mid(int i, int mm) : Base(i), m(mm) { log_[nlog++] = 2; } ~Mid() { log_[nlog++] = -2; } int both() const { return id + m; } };
struct Derived : Mid { long d; Derived() : Mid(1, 2), d(3) { log_[nlog++] = 3; } ~Derived() { log_[nlog++] = -3; } long sum() const { return get() + m + d; } };
struct A { int a; int ga() const { return a; } };
struct B { int b; int gb() const { return b; } };
struct C : A, B { int c; int total() const { return a + b + c; } };
struct Holder { Base inner; int extra; Holder(int x) : inner(x), extra(x * 2) { log_[nlog++] = 4; } ~Holder() { log_[nlog++] = -4; } };
struct Dflt { int v; Dflt() : v(9) { } };
struct UsesDflt : Dflt { int w; UsesDflt() : w(1) { } };
int use_base(const Base &b) { return b.get(); }
int use_b(const B &b) { return b.gb(); }
int through_ptr(B *p) { return p->b; }
int main() {
	{
		Derived d;
		if (d.sum() != 6 || d.both() != 3 || d.get() != 1 || d.id != 1 || d.m != 2) return 1;
		d.set(10); if (d.sum() != 15) return 2;
		if (use_base(d) != 10) return 3;
		Base *bp = &d; if (bp->get() != 10) return 4;
		Mid &mr = d; if (mr.both() != 12) return 5;
	}
	if (nlog != 6 || log_[0] != 1 || log_[1] != 2 || log_[2] != 3 || log_[3] != -3 || log_[4] != -2 || log_[5] != -1) return 6;
	C c; c.a = 1; c.b = 2; c.c = 3;
	if (c.total() != 6 || c.ga() != 1 || c.gb() != 2) return 7;
	if (use_b(c) != 2 || through_ptr(&c) != 2) return 8;
	B *pb = &c; A *pa = &c; if ((char *)pb - (char *)pa != 4) return 9;
	nlog = 0;
	{ Holder h(5); if (h.inner.get() != 5 || h.extra != 10) return 10; }
	if (nlog != 4 || log_[0] != 1 || log_[1] != 4 || log_[2] != -4 || log_[3] != -1) return 11;
	UsesDflt u; if (u.v != 9 || u.w != 1) return 12;
	if (sizeof(Derived) != 16 || sizeof(C) != 12) return 13;
	return 0;
}
