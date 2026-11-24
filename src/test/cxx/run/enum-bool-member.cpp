// [dcl.enum]/5, [class.mem]/15: a scoped enumeration whose underlying type
// is bool takes one byte and can be a non-static data member.
struct Arc {
    enum class Type : bool { kArc, kWedge };
    Arc() = default;
    float s = 0;
    Type t = Type::kArc;
    static Arc make(float v, bool wedge) {
        Arc a;
        a.s = v;
        if (wedge)
            a.t = Type::kWedge;
        return a;
    }
    friend bool operator==(const Arc &a, const Arc &b) {
        return a.s == b.s && a.t == b.t;
    }
};
enum Plain : bool { p0, p1 };
struct P { Plain x; char c; };
static_assert(sizeof(Arc::Type) == 1);
static_assert(sizeof(P) == 2);
static_assert(sizeof(Arc) == 8);
int holder_check();
int main()
{
    Arc a = Arc::make(1, true), b = Arc::make(1, false);
    if (a == b)
        return 1;
    if (!(a == Arc::make(1, true)))
        return 2;
    return holder_check();
}

// [dcl.init]/17.6.1, [class.base.init]/9.1: a default member initializer
// that is a prvalue of the member's own (trivial) class type.
struct Rect {
    float l = 0, t = 0, r = 0, b = 0;
    static Rect make(float v) { return Rect{v, v, v, v}; }
};
struct Holder {
    Rect o = Rect::make(3);
    int x = 4;
};
int holder_check()
{
    Holder h;
    if (h.o.r != 3 || h.x != 4)
        return 3;
    return 0;
}
