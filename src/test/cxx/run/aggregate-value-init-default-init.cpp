// [dcl.init]/9, [expr.type.conv]/2: T() of an aggregate whose members have
// default member initializers value-initializes it (the implicit default
// constructor runs the initializers), also bound to a const reference
// ([dcl.init.ref]/5.4).
struct Tag {
    int a = 3;
    int b = 4;
};
struct Plain {
    int x;
    int y;
};
static int take(const Tag &t)
{
    return t.a + t.b;
}
int main()
{
    Tag t = Tag();
    if (t.a != 3 || t.b != 4)
        return 1;
    if (take(Tag()) != 7)
        return 2;
    Plain p = Plain();
    if (p.x != 0 || p.y != 0)
        return 4;
    const Tag &r = Tag();
    if (r.a != 3)
        return 5;
    const Plain &q = Plain();
    if (q.x != 0 || q.y != 0)
        return 6;
    const Plain &z = Plain{5, 6};
    if (z.x != 5 || z.y != 6)
        return 7;
    return 0;
}
