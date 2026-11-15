// two bases with vtables: the second's is a secondary vtable of the group, its slots this class's overriders
// through this-adjusting thunks (_ZThn16_...), destructors' too; calls through either base, deletes through
// either (llvm-test-suite pointer_method2)
static int dtors;
struct A { int a; virtual int fa() { return 1; } virtual ~A() { dtors += 1; } };
struct B { int b; virtual int fb() { return 2; } virtual int g(int k) { return k; } virtual ~B() { dtors += 10; } };
struct C : A, B { int c; int fb() { return 30 + c; } int g(int k) { return k * c; } ~C() { dtors += 100; } };
int main() {
	C x;
	x.c = 5;
	A *pa = &x;
	B *pb = &x;
	int r = pa->fa() + pb->fb() + pb->g(3) + x.fb() + x.g(1);
	B *h = new C;
	static_cast<C *>(h)->c = 2;
	int r2 = h->fb() + h->g(4);
	delete h;
	A *k = new C;
	delete k;
	return r == 1 + 35 + 15 + 35 + 5 && r2 == 32 + 8 && dtors == 222 ? 0 : 1;
}
