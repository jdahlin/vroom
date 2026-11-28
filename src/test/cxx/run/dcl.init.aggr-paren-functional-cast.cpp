/* [expr.type.conv]/2, [dcl.init.general]/16.6.2.2: T(a, b) of an aggregate is
   a parenthesized aggregate initialization, which allows narrowing; and
   [class.union.anon] a const anonymous union member. */
/* cxx-run-flags: -std=c++20 */
struct Rect {
    int left, top, right = 7, bottom = 9;
};

struct Entry {
    const union {
        int station;
        struct {
            short cargo;
            bool transfers;
        };
    };
    int count;
    Entry(int s, int n) : station(s), count(n) {}
};

static int sum(const Rect &r)
{
    return r.left + r.top + r.right + r.bottom;
}

int main()
{
    unsigned h = 3;
    int y = 10;
    Rect r = Rect(h, y);
    if (sum(r) != 29)
        return 1;
    if (sum(Rect(r.left + h, y, r.right - h, y + h - 1)) != 32)
        return 2;
    Entry e(65537, 2);
    if (e.station != 65537 || e.cargo != 1 || e.count != 2)
        return 3;
    return 0;
}
