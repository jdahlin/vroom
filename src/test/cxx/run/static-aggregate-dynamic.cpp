/* [basic.start.dynamic], [dcl.init.aggr]: an aggregate of static storage
   duration whose initializer has a member that is not a constant
   expression is initialized dynamically: at namespace scope before main,
   at block scope under its guard the first time control passes through
   its declaration; its value is the braced-init-list's object. */
extern "C" int printf(const char *, ...);

int counter = 3;
int next() { return ++counter; }

struct Point {
    int x, y;
    double scale;
};

Point origin = {next(), 2, 1.5};

int local()
{
    static Point p = {next(), next(), 0.5};
    return p.x * 10 + p.y;
}

int main()
{
    int a = local(), b = local();
    printf("%d %d %d %d %g\n", origin.x, origin.y, a, b, origin.scale);
    return a == b && origin.x == 4 ? 0 : 1;
}
