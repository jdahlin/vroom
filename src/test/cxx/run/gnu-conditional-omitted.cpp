// [ext:gnu_conditional_omitted] a ? : c: a evaluated once, its value the result when nonzero
static int calls;
static int next(int v) { ++calls; return v; }
static double half(double v) { ++calls; return v; }
static int *pointer(int *p) { ++calls; return p; }
enum color { none, red = 3 };
static color pick(color c) { ++calls; return c; }

template <class T, class U> struct same {
    static constexpr bool value = false;
};
template <class T> struct same<T, T> {
    static constexpr bool value = true;
};
static char c1;
static float f1;
static color k1;
static char fc();
static_assert(same<decltype(c1 ?: c1), char &>::value, "lvalues of one type");
static_assert(same<decltype(f1 ?: f1), float &>::value, "");
static_assert(same<decltype(k1 ?: k1), color &>::value, "");
static_assert(same<decltype(fc() ?: fc()), int>::value, "promoted");
static_assert(same<decltype(k1 ?: none), int>::value, "an enumeration promoted");
static_assert(same<decltype(c1 ?: 'a'), int>::value, "");
static_assert(same<decltype(f1 ?: 1), float>::value, "");

static_assert((7 ?: 2) == 7, "");
static_assert((0 ?: 2) == 2, "");
static_assert((0 ?: 0 ?: 6) == 6, "");

int main() {
    int fails = 0;
    int r = 10;
    // arithmetic: the common type of the operands
    int a = next(5) ?: r, b = next(0) ?: r;
    fails += !(a == 5 && b == 10 && calls == 2);
    double d = half(0.5) ?: 3;
    double e = half(0.0) ?: 3;
    long l = next(0) ?: 7L;
    fails += !(d == 0.5 && e == 3.0 && l == 7 && calls == 5);
    // a narrower first operand converts to the common type
    unsigned char c = 200;
    unsigned wide = c ?: 5u;
    fails += !(wide == 200u);
    // pointers and enumerations
    int x = 1;
    int *p = pointer(&x) ?: &r;
    int *q = pointer(nullptr) ?: &r;
    int k = pick(red) ?: none, m = pick(none) ?: red;
    fails += !(p == &x && q == &r && k == red && m == red && calls == 9);
    // a chain groups to the right
    int chain = next(0) ?: next(0) ?: next(8);
    fails += !(chain == 8 && calls == 12);
    // lvalues of one type: an lvalue
    int u = 0, v = 4;
    (u ?: v) = 9;
    int &ref = u ?: v;
    ref += 1;
    fails += !(u == 0 && v == 10);
    int w = 3;
    (w ?: v) = 11;
    fails += !(w == 11 && v == 10);
    // the operand's side effect happens once, as an lvalue too
    int i = 0, values[3] = {0, 2, 0};
    int &chosen = values[i++] ?: values[i++];
    fails += !(i == 2 && &chosen == &values[1]);
    // in a larger expression
    int total = (next(0) ?: 1) + (next(2) ?: 100);
    fails += !(total == 3);
    return fails;
}
