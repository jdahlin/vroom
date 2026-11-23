// default arguments of constructors: [dcl.fct.default], [class.mem]/6,
// [over.match.ctor]
#include <stdio.h>

struct Range {
    int low, high, step;
    Range(int h = limit, int l = 0, int s = 1) : low(l), high(h), step(s) {}
    int count() const { return (high - low) / step; }
    enum { limit = 10 }; // declared after its use above
};

struct Pair {
    Range a, b;
    Pair() : a(4), b(20, 10, 2) {} // the bases' and members' constructors
};

struct Tag {
    int v;
    Tag(int x, int scale = 3);
};
Tag::Tag(int x, int scale) : v(x * scale) {}

int main()
{
    Range r;              // the default constructor: Range(10, 0, 1)
    Range s(6);           // Range(6, 0, 1)
    Range t(9, 3);        // Range(9, 3, 1)
    Range u(20, 0, 5);
    int total = r.count() + s.count() * 10 + t.count() * 100 + u.count() * 1000;
    Pair p;
    total += p.a.count() * 10000 + p.b.count() * 100000;
    Tag g(7), h(7, 2);
    total += g.v + h.v;   // 21 + 14
    printf("%d\n", total);
    return total & 255;
}
