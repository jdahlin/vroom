/* [class.base.init]/7, [class.copy.ctor]/14: a member of a class with constructors
   initialized by its implicitly-defined copy constructor, from the
   constructor's parameter of the same name (syntax.h's SyntaxRef: n(n)) */
struct node {
    unsigned v;
    node() : v(0) {}
    explicit node(unsigned x) : v(x) {}
};
struct ref {
    node n;
    int extra;
    ref(node n) : n(n), extra(7) {}
    ref(const ref &o) = default;
};
struct cref {
    const node n;
    cref(const node &n) : n(n) {}
};
int main()
{
    node a(41);
    ref r(a);
    ref r2(r);
    cref c(a);
    return r.n.v - 41 + r.extra - 7 + r2.n.v - 41 + c.n.v - 41;
}
