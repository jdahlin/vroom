/* [expr.const]: a union assigned inside a callee into an older,
   indeterminate one keeps its member after the call returns */
struct R { const char *p; int n; };
union U { int i; R r; };
struct H { U u; };
#define CHECK(name, ...) constexpr int name() { __VA_ARGS__ const char *lit = "wxyzwxyz"; h.u.r.n -= 1; return h.u.r.n * 10 + (lit[0] == 'w') + (h.u.r.p[0] == 'a'); } static_assert(name() == 12);
constexpr void a(H &h) { h.u = U{.r = R{"ab", 2}}; }
CHECK(fa, H h; a(h);)
constexpr void b(H &h) { U t{.r = R{"ab", 2}}; h.u = t; }
CHECK(fb, H h; b(h);)
constexpr void c(H &h) { H t{U{.r = R{"ab", 2}}}; h = t; }
CHECK(fc, H h; c(h);)
struct K { H h; constexpr K() { U t{.r = R{"ab", 2}}; h.u = t; } };
CHECK(fd, K k; H &h = k.h;)
struct L { H h; constexpr L(U t) : h{t} {} };
constexpr L mk() { U t{.r = R{"ab", 2}}; return L(t); }
CHECK(fe, L l = mk(); H &h = l.h;)
struct M { U u; constexpr M() : u{.i = 0} { u.r = R{"ab", 2}; } };
CHECK(fg, M h;)
struct N { U u; constexpr N() { u.r = R{"ab", 2}; } };
CHECK(fh, N h;)
int main()
{
    return fa() + fb() + fc() + fd() - 48;
}
