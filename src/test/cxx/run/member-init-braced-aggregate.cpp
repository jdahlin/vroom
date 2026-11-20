/* [class.base.init]/9.1, [dcl.init.aggr]/5.1: a member of aggregate class
   type (implicit constructors, default member initializers) whose default
   member initializer is a braced-init-list, in an implicit, a
   user-provided and an aggregate-initialized enclosing class. */
struct C {
    int a = 7;
    int b;
};
struct Plain {
    int x, y;
};
struct Nest {
    C c = {};
    Plain p = {};
};
struct Impl {
    C c = {};
    int z = 3;
};
struct Braced {
    C c{};
    Plain p{1, 2};
    int z = 3;
    Braced() {}
};
struct Listed {
    C c = {1, 2};
    int z;
    Listed() : z(5) {}
};
struct Agg {
    C c = {};
    Nest n = {};
    int q;
};
struct Overridden {
    C c = {};
    int k = 1;
    Overridden(int v) : c{v, v} {}
};
int main()
{
    Impl i;
    Braced b;
    Listed l;
    Agg agg{};
    Agg given{{4, 5}, {}, 9};
    Overridden o(8);
    int bad = 0;
    if (i.c.a != 7 || i.c.b != 0 || i.z != 3)
        bad |= 1;
    if (b.c.a != 7 || b.c.b != 0 || b.p.x != 1 || b.p.y != 2 || b.z != 3)
        bad |= 2;
    if (l.c.a != 1 || l.c.b != 2 || l.z != 5)
        bad |= 4;
    if (agg.c.a != 7 || agg.c.b != 0 || agg.n.c.a != 7 || agg.n.p.y != 0 ||
        agg.q != 0)
        bad |= 8;
    if (given.c.a != 4 || given.c.b != 5 || given.n.c.a != 7 || given.q != 9)
        bad |= 16;
    if (o.c.a != 8 || o.c.b != 8 || o.k != 1)
        bad |= 32;
    return bad;
}
