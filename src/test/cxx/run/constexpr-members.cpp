/* [dcl.constexpr]: constexpr member functions and constructors, called
   at run time (their evaluation in constant expressions is S6's) */
#include <stdio.h>

struct Point {
    constexpr Point(int x, int y) : x(x), y(y) {}
    constexpr int sum() const { return x + y; }
    constexpr int scaled(int by) const;
    int x, y;
};

constexpr int Point::scaled(int by) const
{
    return sum() * by;
}

struct Counter {
    constexpr Counter() : n(0) {}
    int bump() { return ++n; }
    int n;
};

int use(Point p)
{
    return p.sum();
}

int main()
{
    Point p(3, 4);
    Counter c;
    c.bump();
    c.bump();
    printf("%d %d %d %d %d\n", p.sum(), p.scaled(3), Point(1, 2).sum(),
           use(Point(5, 6)), c.n);
    return 0;
}
