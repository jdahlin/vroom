/* [class.copy.elision]/1, [expr.cond]/6: a ?: prvalue's operands
   initialize its result object, destroyed once */
int made, gone;
struct V {
    int x;
    V(int x) : x(x)
    {
        made++;
    }
    V(const V &o) : x(o.x + 1)
    {
        made++;
    }
    ~V()
    {
        gone++;
        x = -1;
    }
};
V two(bool b)
{
    return b ? V(1) : V(2);
}
V mixed(bool b, const V &q)
{
    return b ? V(3) : q;
}
int main()
{
    int bad = 0;
    {
        V a = two(true);
        V q(20);
        V d = mixed(true, q);
        V e = mixed(false, q);
        V f = a.x == 1 ? V(5) : V(6);
        bad = !(a.x == 1 && d.x == 3 && e.x == 21 && f.x == 5 && gone == 0);
    }
    return bad || made != gone;
}
