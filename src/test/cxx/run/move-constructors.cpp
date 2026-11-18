/* [class.copy.ctor]/3: move constructors; [class.copy.elision]/3: a
   returned local or parameter is moved, not copied */
int copies, moves;
struct M {
    int x;
    M(int v) : x(v) {}
    M(const M &o) : x(o.x) { copies++; }
    M(M &&o) : x(o.x) { o.x = -1; moves++; }
};
M from_parameter(M p) { return p; }
M from_local(int v, bool which)
{
    M a(v), b(v + 1);
    if (which)
        return a;
    return (b);
}
M take(M &&r) { return static_cast<M &&>(r); }
int main()
{
    M a(4);
    M b(static_cast<M &&>(a));
    if (a.x != -1 || b.x != 4 || moves != 1)
        return 1;
    M c(b);
    if (copies != 1 || c.x != 4)
        return 2;
    M d = from_parameter(c);
    if (copies != 2 || moves != 2 || d.x != 4)
        return 3;
    M e = from_local(7, false);
    if (e.x != 8 || moves != 3)
        return 4;
    M f = take(static_cast<M &&>(e));
    if (f.x != 8 || e.x != -1 || moves != 4)
        return 5;
    return 0;
}
