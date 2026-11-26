// [class.copy.elision], [abi:calls]: a class that is not trivially copyable is
// built where it ends up, never copied byte for byte: each object keeps its
// own address.
extern "C" int printf(const char *, ...);
struct self {
    self *me; int v;
    self() : me(this), v(1) {}
    self(int x) : me(this), v(x) {}
    self(self &&o) : me(this), v(o.v) {}
    self(const self &o) : me(this), v(o.v) {}
    self &operator=(self &&o) { v = o.v; return *this; }
};
struct C { self s; C() = default; C(C &&) = default; C &operator=(C &&) = default; };
C named() { C c; c.s.v = 2; return c; }
self prvalue() { return self(3); }
self forward() { return prvalue(); }
self from_ref(self &a) { return a; }
C two(int k) { C a, b; b.s.v = 5; if (k) return a; return b; }
struct holder { C c; holder() : c(named()) {} };
int ok(const self &s) { return s.me == &s; }
int take(self s) { return ok(s) && s.v == 3; }
int main() {
    int bad = 0;
    C a = named(); if (!ok(a.s) || a.s.v != 2) bad |= 1;
    self b = prvalue(); if (!ok(b) || b.v != 3) bad |= 2;
    self c = forward(); if (!ok(c) || c.v != 3) bad |= 4;
    self d = from_ref(b); if (!ok(d) || d.v != 3) bad |= 8;
    C e = two(0); if (!ok(e.s) || e.s.v != 5) bad |= 16;
    C f = two(1); if (!ok(f.s) || f.s.v != 1) bad |= 32;
    holder h; if (!ok(h.c.s)) bad |= 64;
    if (!take(prvalue())) bad |= 128;
    a = named(); if (!ok(a.s)) bad |= 512;
    if (!ok(named().s) || !ok(forward())) bad |= 1024;
    if (bad) printf("bad %d\n", bad);
    return bad != 0;
}
