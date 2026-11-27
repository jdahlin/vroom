/* [class.base.init]/7: an aggregate base class initialized by a
   braced-init-list. */
extern "C" int printf(const char *, ...);
struct Raw {
    const int *p;
    int n;
    float bounds[2];
    unsigned char fill, convex;
};
struct Pair { int a, b; };
const int data[] = {1, 2, 3};
struct Triangle : Raw {
    int extra;
    Triangle(int n) : Raw{data, n, {}, 4}, extra(n + 1) {}
};
struct Two : Pair, Raw {
    Two(int v) : Pair{v}, Raw{.p = data + 1, .fill = 9, .convex = 2} {}
};
struct C : Pair {
    constexpr C(int v) : Pair{v, v + 1} {}
};
static_assert(C(3).b == 4);
/* tail padding a virtual base of a more derived class may use */
struct Padded {
    long a = 1;
    int b;
    char c = 2;
};
struct V { char v = 'v'; };
struct Mid : Padded {
    Mid(int x) : Padded{x, x + 1} {}
};
struct Most : Mid, virtual V {
    Most() : Mid(10) {}
};
int main()
{
    Most m;
    printf("%ld %d %d %c %d\n", m.a, m.b, m.c, m.v, static_cast<int>(sizeof m));
    Triangle t(3);
    printf("%d %d %d %d %d %d\n", t.p[0], t.n, static_cast<int>(t.bounds[1]),
           t.fill, t.convex, t.extra);
    Two w(7);
    printf("%d %d %d %d %d %d\n", w.a, w.b, w.p[0], w.n, w.fill, w.convex);
    constexpr C c(5);
    printf("%d %d\n", c.a, c.b);
}
