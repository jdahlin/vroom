/* [expr.static.cast], [expr.const.cast], [expr.reinterpret.cast],
   [expr.cast], [expr.type.conv]: the named casts, the cast notation and the
   functional one, through conversion functions and constructors, between
   pointers and references to classes one derived from the other */
struct Int {
    int v;
    operator int() const { return v; }
};
struct Flag {
    int on;
    explicit operator bool() const { return on != 0; }
};
struct Box {
    int x;
    Box(int y) : x(y * 2) {}
};
struct Base {
    int b;
};
struct Other {
    int c;
};
struct Derived : Base, Other {
    int d;
};
struct Deeper : Derived {
    int e;
};
typedef Box Alias;

static int cast_int(Int i) { return static_cast<int>(i) + int(i) + (int)i; }
static long cast_long(Int i) { return static_cast<long>(i) + (long)i; }
static bool cast_flag(Flag f) { return static_cast<bool>(f) && (bool)f; }
static int boxes()
{
    return static_cast<Box>(3).x + ((Box)4).x + Box(5).x + Alias(6).x;
}
static Derived *down(Other *o) { return static_cast<Derived *>(o); }
static Derived *down_cast_notation(Other *o) { return (Derived *)o; }
static Other *up(Derived *d) { return static_cast<Other *>(d); }
static Deeper *down_two(Other *o) { return static_cast<Deeper *>(o); }
static Derived &down_reference(Other &o) { return static_cast<Derived &>(o); }
static Other &up_reference(Derived &d) { return (Other &)d; }
static const Derived *down_const(const Other *o)
{
    return static_cast<const Derived *>(o);
}
static Derived *down_and_const(const Other *o) { return (Derived *)o; }
static int *unconst(const int *p) { return const_cast<int *>(p); }
static int &unconst_reference(const int &r) { return const_cast<int &>(r); }
static unsigned long address(int *p)
{
    return reinterpret_cast<unsigned long>(p);
}
static unsigned &pun(int &i) { return reinterpret_cast<unsigned &>(i); }
static int &&moved(int &i) { return static_cast<int &&>(i); }

int main()
{
    Int i = {7};
    if (cast_int(i) != 21 || cast_long(i) != 14)
        return 1;
    Flag yes = {1}, no = {0};
    if (!cast_flag(yes) || cast_flag(no))
        return 2;
    if (boxes() != 36)
        return 3;
    Deeper x;
    x.b = 1;
    x.c = 2;
    x.d = 3;
    x.e = 4;
    Other *o = &x;
    Derived *dx = &x;
    Deeper *xx = &x;
    Other *none = 0;
    if (down(o) != dx || down_cast_notation(o) != dx || up(dx) != o)
        return 4;
    if (down(o)->d != 3 || down_two(o)->e != 4 || down_two(o) != xx)
        return 5;
    if (down(none) != 0 || down_two(none) != 0 || up(0) != 0)
        return 6;
    if (&down_reference(*o) != dx || &up_reference(x) != o)
        return 7;
    if (down_const(o) != dx || down_and_const(o) != dx)
        return 8;
    const int k = 9;
    int n = 10;
    if (*unconst(&n) != 10 || &unconst_reference(n) != &n)
        return 9;
    unconst_reference(n) = 11;
    if (n != 11 || unconst(&k) != &k)
        return 10;
    if (reinterpret_cast<int *>(address(&n)) != &n)
        return 11;
    pun(n) = 12u;
    if (n != 12)
        return 12;
    int &&r = moved(n);
    r = 13;
    if (n != 13 || reinterpret_cast<char *>(&n) != (char *)&n)
        return 13;
    static_cast<void>(n);
    Base *bx = &x;
    if ((Base *)xx != bx || static_cast<Base *>(down(o)) != bx)
        return 14;
    return 0;
}
