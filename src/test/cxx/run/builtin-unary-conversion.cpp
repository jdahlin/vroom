// [over.match.oper]/3.3, [over.built]: the built-in unary operators' candidates
// for a class operand that converts to the parameter type; subscript and
// comma.
struct P {
    int a[8];
    operator int *() { return a; }
};
struct I {
    operator int() const { return 4; }
};
struct E {
    operator unsigned char() const { return 5; }
};
struct F {
    operator double() const { return 2.5; }
};

static_assert(__is_same(decltype(+E()), int), "");
static_assert(__is_same(decltype(-F()), double), "");
static_assert(__is_same(decltype(*P()), int &), "");

int main()
{
    P p = {};
    p.a[0] = 7;
    int &r = *p;
    r += 1;
    if (p.a[0] != 8)
        return 1;
    if (+I() != 4 || -I() != -4 || ~I() != -5)
        return 2;
    if (~E() != -6 || -F() != -2.5)
        return 3;
    if (p[1] != 0 || 1 [p] != 0)
        return 4;
    int k = (p, 5);
    if (k != 5 || (I(), 6) != 6)
        return 5;
    I i;
    p[i] = 9;
    if (p.a[4] != 9)
        return 6;
    return 0;
}
