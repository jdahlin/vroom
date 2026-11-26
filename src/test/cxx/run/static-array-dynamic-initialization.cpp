/* [basic.start.dynamic], [stmt.dcl]/4: arrays of static storage duration
   whose braced initializers are not all constant: each element initialized
   in order, once, the trailing ones as [dcl.init.aggr]/5 says */
static int made, destroyed, calls;

static int next(int v)
{
    calls++;
    return v;
}

struct S {
    int v;
    S() : v(-1)
    {
        made++;
    }
    S(int x) : v(x)
    {
        made++;
    }
    ~S()
    {
        destroyed++;
    }
};

struct P {
    int a;
    S s;
};

int scalars[4] = {next(1), 2, next(3)};
int nested[2][2] = {{next(4), 5}, {6}};
S objects[3] = {next(7), 8};
P pairs[2] = {{next(9), 10}, {11, next(12)}};

static int local(int k)
{
    static int a[3] = {next(k), next(k + 1)};
    static const P p[] = {{1, next(k)}, {next(2), 3}};
    return a[0] * 100 + a[1] * 10 + a[2] + p[0].s.v * 1000 + p[1].a * 10000;
}

int main()
{
    if (scalars[0] != 1 || scalars[1] != 2 || scalars[2] != 3 || scalars[3])
        return 1;
    if (nested[0][0] != 4 || nested[0][1] != 5 || nested[1][0] != 6 ||
        nested[1][1])
        return 2;
    if (objects[0].v != 7 || objects[1].v != 8 || objects[2].v != -1)
        return 3;
    if (pairs[0].a != 9 || pairs[0].s.v != 10 || pairs[1].a != 11 ||
        pairs[1].s.v != 12)
        return 4;
    if (made != 5 || calls != 5)
        return 5;
    if (local(5) != 25560 || local(8) != 25560)
        return 6;
    if (made != 7 || calls != 9 || destroyed)
        return 7;
    return 0;
}
