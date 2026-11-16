// std: c++20
// c ? nullptr : sp(...): [expr.cond]/4.6, the one conversion that can be formed (nullptr to sp, its
// converting constructor) is applied to that operand; the result a prvalue of sp (Skia's
// setBlender(mode == kSrcOver ? nullptr : SkBlender::Mode(mode)))
typedef decltype(nullptr) nullptr_t;
static int made, copies;
struct B { int r; };
struct sp {
	B *p;
	sp(nullptr_t) : p(nullptr) { made++; }
	sp(B *q) : p(q) { made++; }
	sp(const sp &o) : p(o.p) { copies++; }
	~sp() {}
};
static B one{7};
static sp Mode(int) { return sp(&one); }
static int take(sp s) { return s.p ? s.p->r : -1; }
int main()
{
	int a = take(1 == 1 ? nullptr : Mode(1)), b = take(1 == 2 ? nullptr : Mode(1));
	sp c = 0 ? Mode(2) : nullptr;
	if (a != -1 || b != 7 || c.p)
		return 1;
	return made == 3 ? 0 : 2;
}
