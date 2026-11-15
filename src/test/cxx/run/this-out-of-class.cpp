// this in a member function defined outside its class (a const one's: const X *)
struct F {
	int a;
	int h();
	F *self();
	int c() const;
	F &inc(int k);
	static int s(int k);
};
int F::h() { return this->a; }
F *F::self() { return this; }
int F::c() const { const F *p = this; return p->a * 2; }
F &F::inc(int k) { this->a += k; return *this; }
int F::s(int k) { return k + 1; }
int main() { F f; f.a = 3; return f.h() == 3 && f.self() == &f && f.c() == 6 && f.inc(2).inc(1).a == 6 && F::s(1) == 2 ? 0 : 1; }
