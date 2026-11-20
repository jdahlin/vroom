/* [class.conv.fct]/4 with [over.match.conv]/1: the conversion functions of
   a class's bases are candidates too, unless the class (or a nearer base)
   has one to the same type that hides them ([class.member.lookup]); the
   implied object argument is converted to the base subobject
   ([over.match.funcs]/4-5), at an offset when the base is not the first. */
struct Pad {
    long pad;
};
struct Flag {
    int on;
    explicit operator bool() const { return on != 0; }
};
struct Number {
    int n;
    operator int() const { return n; }
};
struct Ptr {
    const char *p;
    operator const char *() const { return p; }
};
/* the base's function through a derived class, at offset 0 */
struct FlagUser : Flag {
    int more;
};
/* the base is not at offset 0 */
struct Late : Pad, Number {
    int other;
};
struct LatePtr : Pad, Flag, Ptr {
    int other;
};
/* hidden: the derived class's conversion to the same type wins */
struct Hiding : Number {
    int m;
    operator int() const { return m * 10; }
};
/* not hidden: a different type, and the base's int is the one a long needs
   when the derived has only a pointer */
struct Other : Number {
    const char *text;
    operator const char *() const { return text; }
};
/* two levels */
struct Deep : Late {
    int last;
};
/* a base conversion function returning a reference */
struct Slot {
    int v;
    operator int &() { return v; }
};
struct SlotUser : Pad, Slot {};

/* a different type in the derived class: both are candidates, and the
   implied object parameter is the derived class's for both
   ([over.match.funcs]/4), so the target type decides */
struct XB {
    int base;
    operator int() { return base; }
};
struct Yb : Pad, XB {
    char derived;
    operator char() { return derived; }
};

static int take(int x) { return x; }
static const char *take_ptr(const char *x) { return x; }

int main()
{
    int bad = 0;
    FlagUser fu;
    fu.on = 1;
    fu.more = 5;
    if (fu)
        bad |= 1;
    if (static_cast<bool>(fu) != true)
        bad |= 2;
    Late l;
    l.pad = 9;
    l.n = 42;
    l.other = 3;
    int a = l;
    if (a != 42)
        bad |= 4;
    if (take(l) != 42)
        bad |= 8;
    LatePtr lp;
    lp.on = 0;
    lp.p = "hello";
    if (take_ptr(lp)[1] != 'e')
        bad |= 32;
    if (static_cast<bool>(lp))
        bad |= 64;
    Hiding h;
    h.n = 1;
    h.m = 4;
    int hv = h;
    if (hv != 40)
        bad |= 128;
    Other o;
    o.n = 7;
    o.text = "xy";
    const char *s = o;
    int ov = o;
    if (s[1] != 'y' || ov != 7)
        bad |= 256;
    Deep d;
    d.n = 11;
    int dv = d;
    if (dv != 11)
        bad |= 512;
    const Late cl = l;
    int cv = cl;
    if (cv != 42)
        bad |= 1024;
    SlotUser su;
    su.v = 3;
    int &r = su;
    r = 8;
    if (su.v != 8)
        bad |= 2048;
    int sv = su;
    if (sv != 8)
        bad |= 4096;
    Yb y;
    y.base = 300;
    y.derived = 'k';
    int yi = y;
    char yc = y;
    if (yi != 300 || yc != 'k')
        bad |= 8192;
    return bad;
}
