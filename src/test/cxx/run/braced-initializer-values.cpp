// braced initializers and value-initialization as values: a temporary, an
// argument, an assignment's source, a conditional's arm, a result
// [dcl.init.list], [expr.type.conv], [class.temporary]
struct P { int a, b; };
struct Q { P p; long l; double d; char c[5]; };
struct E { };
struct Z { int x[8]; };
typedef long double LD;

__attribute__((noinline)) int sum(P p) { return p.a * 10 + p.b; }
__attribute__((noinline)) long qsum(const Q &q) {
    return q.p.a + q.p.b + q.l + (long)q.d + q.c[0] + q.c[4];
}
__attribute__((noinline)) int zsum(Z z) {
    int s = 0;
    for (int i = 0; i < 8; i++)
        s += z.x[i];
    return s;
}
__attribute__((noinline)) P make(int a, int b) { return P{a, b}; }
__attribute__((noinline)) int pick(bool c) {
    P p = c ? P{1, 2} : P{3, 4};
    return p.a * 10 + p.b;
}

int main() {
    int k = 7;
    long r = 0;
    P p{};
    p = P{k, k + 1};
    r += p.a * 100 + p.b;
    r += sum(P{3, 4});
    r += sum(P{});
    r += sum({5, 6});
    r += qsum(Q{{1, 2}, 30, 4.5, {7}});
    r += qsum(Q{});
    r += zsum(Z{});
    r += zsum(Z{{1, 2, 3}});
    r += make(8, 9).b;
    r += pick(true) + pick(false);
    Z z = {};
    z = Z{{k, k}};
    r += zsum(z);
    E e = E{};
    (void)e;
    double d = double{};
    LD ld = LD{};
    r += (long)d + (long)ld;
    P *pp = &(p = P{k * 2, 1});
    r += pp->a + pp->b;
    r += (k > 5 ? P{k, 0} : P{0, k}).a;
    r += (P{k, k}, P{9, 9}).a;
    return r & 255;
}
