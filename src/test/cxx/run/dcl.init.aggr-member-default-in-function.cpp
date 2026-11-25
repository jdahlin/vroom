/* [dcl.init.aggr]/5.1: default member initializers that name something but
   no `this`, of aggregates initialized in a function body, in an implicit
   constructor and in a template */
constexpr int lowest() { return -7; }
struct P {
    long m = (lowest)();
};
template <class T> struct I {
    T t{};
    P prim{};
};
struct N {
    P prim{};
};
struct H {
    I<int> i;
};
int main()
{
    I<int> i{};
    N n{};
    H h{};
    H d;
    if (i.prim.m != -7 || n.prim.m != -7)
        return 1;
    if (h.i.prim.m != -7 || d.i.prim.m != -7)
        return 2;
    return 0;
}
