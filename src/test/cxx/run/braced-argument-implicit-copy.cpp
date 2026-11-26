/* [over.best.ics]/4, [over.ics.list]/7: T x({a, b}) picks the implicit copy
   or move constructor, its parameter copy-list-initialized from the list */
extern "C" int printf(const char *, ...);
struct pr {
    int a, b;
    pr(int x, int y) : a(x), b(y) {}
};
struct holder {
    pr m;
    holder() : m({5, 6}) {}
};
int main()
{
    pr p({1, 2});
    pr *q = new pr({3, 4});
    holder h;
    pr t = pr({7, 8});
    printf("%d %d %d %d %d %d %d %d\n", p.a, p.b, q->a, q->b, h.m.a, h.m.b,
           t.a, t.b);
    delete q;
    return 0;
}
