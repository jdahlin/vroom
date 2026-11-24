// [over.match.oper]/3.3, [over.built]/3-4, /18-19: ++, -- and the compound
// assignments' built-in candidates take their left operand by lvalue
// reference: a class operand reaches them through a conversion function that
// returns an lvalue reference; the right operand through any.
struct R {
    int v;
    operator int &() { return v; }
};
struct D {
    operator double() const { return 2.5; }
};
struct I {
    operator int() const { return 3; }
};

int main()
{
    R r = {5};
    ++r;
    if (r.v != 6)
        return 1;
    int old = r++;
    if (old != 6 || r.v != 7)
        return 2;
    --r;
    r += 4;
    if (r.v != 10)
        return 3;
    r -= D();
    if (r.v != 7)
        return 4;
    int k = 20;
    k *= D();
    k <<= I();
    if (k != 400)
        return 5;
    r <<= I();
    if (r.v != 56)
        return 6;
    return 0;
}
