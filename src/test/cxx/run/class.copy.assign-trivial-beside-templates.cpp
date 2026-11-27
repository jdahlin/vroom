// [class.copy.ctor]/11, [class.copy.assign]/9, /13.2: a trivial implicit or
// defaulted special member (no entity) beside deleted or template ones, and
// the memberwise assignment of an array of class elements.
struct A {
    int n = 0;
    A() = default;
    A(A &&o) : n(o.n + 1) {}
    A &operator=(A &&o)
    {
        n = o.n + 10;
        return *this;
    }
};
struct OnlyMove {
    OnlyMove() = default;
    OnlyMove(const OnlyMove &) = delete;
    OnlyMove(OnlyMove &&) = default;
};
struct Derived : A, private OnlyMove {
    Derived() = default;
    Derived(Derived &&) = default;
};
static_assert(__is_constructible(Derived, Derived &&));

struct Nothing {
};
/* operator= templates and another type's beside defaulted ones */
template <class T> struct Box {
    T value;
    Box() = default;
    Box(const Box &) = default;
    Box(Box &&) = default;
    Box &operator=(const Box &) = default;
    Box &operator=(Box &&) = default;
    Box &operator=(Nothing);
    template <class U> Box &operator=(U &&);
};
struct Tagged {
    int x;
    Tagged &operator=(Nothing);
};
struct Moves {
    int n = 0;
    Moves() = default;
    Moves(Moves &&) = default;
    Moves &operator=(Moves &&o)
    {
        n = o.n + 1;
        return *this;
    }
};
struct Both {
    Box<int> a;
    Tagged t;
    Moves c;
};
static_assert(__is_assignable(Both &, Both &&));

struct Counted {
    int n = 0;
    Counted &operator=(const Counted &o)
    {
        n = o.n + 1;
        return *this;
    }
    Counted &operator=(Counted &&o)
    {
        n = o.n + 100;
        return *this;
    }
};
struct Arrays {
    Counted a[3];
    Counted b[2];
};

int main()
{
    Derived d;
    Derived e(static_cast<Derived &&>(d));
    if (e.n != 1)
        return 1;

    Both x, y;
    x.a.value = 4;
    x.t.x = 3;
    x.c.n = 5;
    y = static_cast<Both &&>(x);
    if (y.a.value != 4 || y.t.x != 3 || y.c.n != 6)
        return 2;

    Arrays p, q, r;
    p.a[2].n = 5;
    p.b[1].n = 7;
    q = p;
    if (q.a[0].n != 1 || q.a[2].n != 6 || q.b[1].n != 8)
        return 3;
    r = static_cast<Arrays &&>(p);
    if (r.a[2].n != 105 || r.b[1].n != 107)
        return 4;
    return 0;
}
