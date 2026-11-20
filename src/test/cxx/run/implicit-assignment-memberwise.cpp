// The implicitly-defined copy and move assignment operators of a class with a
// member that has its own ([class.copy.assign]/12, /13): bases first, then the
// members in declaration order, each by its own operator= (the move one for
// an xvalue source), scalars by the built-in assignment; the result is *this
// (a = b = c); a self-assignment runs the members' operators too.
extern "C" int printf(const char *, ...);

int copies, moves;
int order[16], steps;

struct Counted {
    int v;
    int id;
    Counted(int i = 0) : v(0), id(i) {}
    Counted &operator=(const Counted &o)
    {
        copies++;
        order[steps++] = id;
        v = o.v;
        return *this;
    }
    Counted &operator=(Counted &&o)
    {
        moves++;
        order[steps++] = id + 100;
        v = o.v;
        return *this;
    }
};

struct Base {
    Counted b;
    Base() : b(1) {}
};
struct Other {
    Counted o;
    Other() : o(2) {}
};
struct Derived : Base, Other {
    Counted x;
    int n;
    Counted y;
    Derived() : x(3), n(0), y(4) {}
};

// move assignment is not declared beside a user-declared destructor: a move
// copies
struct WithDtor {
    Counted c;
    WithDtor() : c(5) {}
    ~WithDtor() {}
};

// a deleted copy assignment operator of a member: the class can be moved only
struct MoveOnlyMember {
    int v;
    MoveOnlyMember() : v(0) {}
    MoveOnlyMember &operator=(const MoveOnlyMember &) = delete;
    MoveOnlyMember &operator=(MoveOnlyMember &&o)
    {
        moves++;
        v = o.v;
        return *this;
    }
};
struct Holder {
    MoveOnlyMember m;
    int tag;
    Holder() : tag(0) {}
};

// a const member deletes both; never assigned here
struct ConstMember {
    const int k;
    Counted c;
    ConstMember() : k(7), c(6) {}
};

// bit-fields and a mutable member go through the built-in assignment
struct Bits {
    Counted c;
    int a : 3;
    unsigned b : 5;
    mutable int m;
    Bits() : c(9), a(0), b(0), m(0) {}
};

struct Arr {
    Counted c;
    int a[3];
    Arr() : c(8) { a[0] = a[1] = a[2] = 0; }
};

// a unique-owner member (deleted copy, move constructor and move
// assignment): the class has an implicit move constructor and move
// assignment operator, its copy assignment operator is deleted
template <typename T> struct Own {
    T *p = nullptr;
    Own() = default;
    ~Own() {}
    Own(const Own &) = delete;
    Own &operator=(const Own &) = delete;
    Own(Own &&o) noexcept : p(o.p) { o.p = nullptr; }
    Own &operator=(Own &&o) noexcept
    {
        moves++;
        p = o.p;
        o.p = nullptr;
        return *this;
    }
};
struct Frame {
    bool active = false;
    Own<int> a;
    unsigned loops = 0;
    Own<long> b;
};

// [except.spec]/8: the implicit operator is noexcept when its subobjects' are
static_assert(!noexcept(static_cast<Derived &>(*(Derived *)0) = *(Derived *)0),
              "Counted's operator= may throw");
static_assert(noexcept(static_cast<Frame &>(*(Frame *)0) =
                           static_cast<Frame &&>(*(Frame *)0)),
              "Own's move assignment is noexcept");

int main()
{
    Derived a, b, c;
    b.b.v = 1;
    b.o.v = 2;
    b.x.v = 3;
    b.n = 4;
    b.y.v = 5;
    c.b.v = 10;
    c.o.v = 20;
    c.x.v = 30;
    c.n = 40;
    c.y.v = 50;
    a = b = c;
    printf("%d %d %d %d %d %d\n", a.b.v, a.o.v, a.x.v, a.n, a.y.v, b.n);
    printf("copies %d moves %d order", copies, moves);
    for (int i = 0; i < steps; i++)
        printf(" %d", order[i]);
    printf("\n");
    int bad = 0;
    bad += a.n != 40 || a.y.v != 50 || b.b.v != 10;
    bad += copies != 8 || moves != 0 || steps != 8;
    // b = c first (right to left), then a = b: bases, x, y in order
    int expected[8] = {1, 2, 3, 4, 1, 2, 3, 4};
    for (int i = 0; i < 8; i++)
        bad += order[i] != expected[i];
    copies = moves = steps = 0;
    a = static_cast<Derived &&>(b);
    bad += copies != 0 || moves != 4;
    bad += order[0] != 101 || order[1] != 102 || order[2] != 103 ||
           order[3] != 104;
    copies = moves = steps = 0;
    a = a;
    bad += copies != 4;
    WithDtor w1, w2;
    w2.c.v = 9;
    copies = moves = steps = 0;
    w1 = static_cast<WithDtor &&>(w2);
    bad += copies != 1 || moves != 0 || w1.c.v != 9;
    Holder h1, h2;
    h2.m.v = 11;
    copies = moves = steps = 0;
    h1 = static_cast<Holder &&>(h2);
    bad += moves != 1 || h1.m.v != 11;
    ConstMember cm1, cm2;
    (void)cm1;
    (void)cm2;
    Arr r1, r2;
    r2.a[0] = 1;
    r2.a[1] = 2;
    r2.a[2] = 3;
    r2.c.v = 12;
    r1 = r2;
    bad += r1.a[2] != 3 || r1.c.v != 12;
    int value = 5;
    Frame f1, f2;
    f1.a.p = &value;
    f1.loops = 3;
    f1.active = true;
    moves = 0;
    f2 = static_cast<Frame &&>(f1);
    bad += f2.a.p != &value || f1.a.p || f2.loops != 3 || !f2.active ||
           moves != 2;
    moves = 0;
    f1 = Frame();
    bad += moves != 2 || f1.a.p;
    Bits g1, g2;
    g2.a = -2;
    g2.b = 21;
    g2.m = 5;
    g2.c.v = 13;
    g1 = g2;
    bad += g1.a != -2 || g1.b != 21 || g1.m != 5 || g1.c.v != 13;
    printf("bad %d\n", bad);
    return bad;
}
