// [over.best.ics]/1, [over.ics.list]: braced-init-list arguments of the
// constructor a new-initializer calls
struct span {
    int *p;
    int n;
    span() : p(nullptr), n(7) {}
    span(int *q, int m) : p(q), n(m) {}
};

struct data {
    int a;
    data(span s, void (*release)(void *), void *context)
        : a(s.n + (release == nullptr) + (context == nullptr))
    {
    }
    data(int x, span s) : a(x * 100 + s.n) {}
};

int main()
{
    static int v[3];
    data *e = new data({}, nullptr, nullptr);
    data *f = new data({v, 3}, nullptr, nullptr);
    data *g = new data(2, {});
    __builtin_printf("%d %d %d\n", e->a, f->a, g->a);
    delete e;
    delete f;
    delete g;
    return 0;
}
