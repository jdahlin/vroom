/* [dcl.fct.def.default]/5: assignment operators defaulted outside the class */
extern "C" int printf(const char *, ...);

struct counted {
    int v = 0;
    int copies = 0, moves = 0;
    counted &operator=(const counted &o)
    {
        v = o.v;
        copies++;
        return *this;
    }
    counted &operator=(counted &&o)
    {
        v = o.v;
        moves++;
        return *this;
    }
};

struct base {
    int b = 0;
};

struct holder : base {
    counted c;
    int a[3] = {};
    holder &operator=(const holder &);
    holder &operator=(holder &&);
};

holder &holder::operator=(const holder &) = default;
holder &holder::operator=(holder &&) = default;

int main()
{
    holder x, y, z;
    x.b = 1;
    x.c.v = 2;
    x.a[2] = 3;
    holder &r = (y = x);
    z = static_cast<holder &&>(x);
    printf("%d %d %d %d %d %d\n", &r == &y, y.b, y.c.v, y.a[2], y.c.copies,
           y.c.moves);
    printf("%d %d %d %d %d\n", z.b, z.c.v, z.a[2], z.c.copies, z.c.moves);
    return !(y.b == 1 && y.c.copies == 1 && z.c.moves == 1 && z.a[2] == 3);
}
