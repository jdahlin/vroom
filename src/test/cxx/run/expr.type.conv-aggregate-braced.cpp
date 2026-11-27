/* [expr.type.conv]/2 and [dcl.init]/17.6.2.1: T({...}) of an aggregate is its
   move constructor's, the list initializing the temporary: the elements
   are the class's, not the first member's */
struct four {
    float a, b, c, d;
};
struct two {
    int a, b;
};
struct nested {
    two first;
    int last;
};

static float sum(const four &f)
{
    return f.a + 10 * f.b + 100 * f.c + 1000 * f.d;
}

int main()
{
    float v[4] = {1, 2, 3, 4};
    four p = four({v[0], v[1], v[2], v[3]});
    if (sum(p) != 4321)
        return 1;
    p = four({v[3] / 2.f, 1});
    if (sum(p) != 12)
        return 2;
    if (sum(four({})) != 0)
        return 3;
    nested n = nested({{5, 6}, 7});
    if (n.first.a != 5 || n.first.b != 6 || n.last != 7)
        return 4;
    nested e = nested({8, 9});
    if (e.first.a != 8 || e.first.b != 9 || e.last != 0)
        return 5;
    return 0;
}
