// [conv]/1, [over.ics.list]: a class type converts to no arithmetic type by a
// standard conversion, so the aggregate P is not viable for {a, b} and the
// overload taking Q is chosen.
struct P { float x, y; };
struct Q { P first, second; };

struct W {
    int which;
    W(P) : which(1) {}
    W(Q) : which(2) {}
};

static int pick(P) { return 1; }
static int pick(Q) { return 2; }

// [dcl.init.list]/3.2 (CWG 1467): an aggregate from a one-element list of a
// derived class copies the base subobject.
struct Base { int i{42}; };
struct Derived : Base {};

int main()
{
    P a = {1.0f, 2.0f};
    P b = {3.0f, 4.0f};
    W w = {{a, b}};
    if (w.which != 2)
        return 1;
    if (pick({a, b}) != 2)
        return 2;
    Derived d;
    d.i = 7;
    Base z{d};
    if (z.i != 7)
        return 3;
    return 0;
}
