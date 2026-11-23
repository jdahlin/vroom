/* The constant evaluator on class objects: constexpr constructors (member
   initializers and bodies), member functions through this, members read
   and written, copies by value, class objects returned, constexpr
   variables of class type ([dcl.constexpr], [class.this], [expr.ref]). */
#include <stdio.h>

struct point {
    int x, y;
    constexpr point(int a, int b) : x(a), y(b) {}
    constexpr int sum() const { return x + y; }
    constexpr void shift(int d)
    {
        x += d;
        this->y -= d;
    }
    constexpr point flipped() const { return point(y, x); }
};

struct box {
    point low, high;
    int tag;
    constexpr box(int n) : low(n, n), high(2 * n, 3 * n), tag(n)
    {
        if (n > 2)
            tag = 0;
        low.shift(tag);
    }
    constexpr int area() const
    {
        return (high.x - low.x) * (high.y - low.y);
    }
};

struct pair { int first; long second; int rest[2]; };

constexpr pair make_pair(int a) { return pair{a, a * 10L, {a, -a}}; }
constexpr int by_value(point p)
{
    p.shift(5);
    return p.x;
}

constexpr int walk(int n)
{
    point p(n, 1);
    for (int i = 0; i < 3; i++)
        p.shift(i);
    point q = p.flipped();
    return q.x * 100 + q.y + by_value(p) - p.x;
}

constexpr point origin(0, 7);
constexpr box b3(3);

static_assert(point(2, 3).sum() == 5, "");
static_assert(walk(4) == -188, "");
static_assert(origin.y == 7 && origin.flipped().x == 7, "");
static_assert(b3.area() == 3 * 6 && b3.tag == 0, "");
static_assert(make_pair(4).second == 40, "");
static_assert(by_value(origin) == 5 && origin.x == 0, "");

int bound[box(3).area() + point(2, 3).sum()];
enum { W = walk(2) };

int main()
{
    int n = 3;
    point p(n, 4);
    p.shift(1);
    printf("%d %d %d %d\n", p.sum(), walk(n), box(n).area(),
           (int)make_pair(n).second);
    printf("%d %d %d %d\n", (int)sizeof bound, (int)W, origin.flipped().x,
           b3.tag);
    return 0;
}
