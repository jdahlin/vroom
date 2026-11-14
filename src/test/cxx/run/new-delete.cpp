// new and delete: objects with constructors and destructors, scalars, arrays, placement new, explicit
// destructor calls, operator new and delete called by name
static int live;
struct P { int x; P(int v) : x(v) { live++; } ~P() { live--; } };
struct Q { int a = 5; long b; };
inline void *operator new(unsigned long, void *p) noexcept { return p; }
static int exprs() {
	P *p = new P(7);
	int r = p->x == 7 && live == 1;
	delete p;
	r = r && live == 0;
	int *a = new int[10]();
	for (int i = 0; i < 10; i++) r = r && a[i] == 0;
	delete[] a;
	int *s = new int(42);
	r = r && *s == 42;
	delete s;
	alignas(P) char buf[sizeof(P)];
	P *q = new (buf) P(9);
	r = r && q->x == 9 && live == 1;
	q->~P();
	Q *z = new Q;
	r = r && z->a == 5;
	delete z;
	return r ? 0 : 1;
}
static int named() { void *p = ::operator new(16); int *q = static_cast<int *>(p); *q = 4; int r = *q; ::operator delete(p); void *a = ::operator new[](8); ::operator delete[](a); return r == 4 ? 0 : 1; }
int main() { return exprs() + 2 * named(); }
