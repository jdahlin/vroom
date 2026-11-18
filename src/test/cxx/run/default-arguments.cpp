// default arguments: [dcl.fct.default], [expr.call]/7, [over.match.viable]/2
#include <stdio.h>

static int calls = 0;
int next() { return ++calls; }

int add(int a, int b = 10, int c = 100) { return a + b + c; }

// /4: added by a later declaration
int scale(int x, int by);
int scale(int x, int by = 3);
int scale(int x, int by) { return x * by; }

// /9: evaluated at each call
int stamp(int n = next()) { return n; }

// overloads with defaults
int pick(int, int = 0) { return 1; }
int pick(double) { return 2; }

namespace N {
const char *name(const char *s = "none") { return s; }
} // namespace N

struct P {
    int x, y;
};
int sum(P p = {1, 2}) { return p.x + p.y; }

int main() {
    int total = add(1) + add(1, 2) + add(1, 2, 3);
    total += scale(4) + scale(4, 5);
    total += stamp() + stamp() + stamp(50);
    total += pick(7) + pick(7.0) + pick(7, 8);
    total += N::name()[0] + N::name("x")[0];
    total += sum() + sum(P{3, 4}) + sum({5, 6});
    printf("%d %d\n", total, calls);
    return total & 255;
}
