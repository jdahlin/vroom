// functions named as values (decayed to pointers), calls through pointers and pointer members, and
// default member initializers in user, implicit and defaulted constructors (doctest's registration)
typedef void (*ft)();
struct TC { ft f; const char *file; TC(ft g, const char *x) : f(g), file(x) {} };
static int regf(const TC &t) { t.f(); return 0; }
static int hits;
static void F();
static const int v = regf(TC(F, "a"));
static void F() { hits++; }
static int fp_main() { ft p = F; p(); ft q = &F; q(); return hits == 3 && v == 0 ? 0 : 1; }
struct TS { const char *a = nullptr; bool s = false; int n = 3; double t = 0; TS &operator*(const char *x) { a = x; return *this; } };
struct D { int k = 8; D() = default; };
struct H { TS ts; D d; int z; };
static int consume2(const int *, int) noexcept { return 0; }
static int reg(TS &t) { return t.n + (t.a[0] == 0); }
static const int v2 = consume2(&v2, reg(TS() * ""));
static int ns_main() { TS x; D d; H h; return v2 == 0 && x.n == 3 && d.k == 8 && h.ts.n == 3 && h.d.k == 8 && reg(TS() * "") == 4 ? 0 : 1; }
struct P2 { int x = 4; P2() {} P2(int v) : x(v) {} };
struct S2 { const char *a = nullptr; bool s = false; int n = 3; double t{}; P2 p{}; P2 q = 9; int k{5}; S2() {} S2(int m) : n(m) {} };
static int ns3_main() { S2 a; S2 b(7); return a.n == 3 && b.n == 7 && a.k == 5 && a.p.x == 4 && b.q.x == 9 && !a.a && a.t == 0 ? 0 : 1; }
int main() { return fp_main() + 2 * ns_main() + 4 * ns3_main(); }
