/* A record built in a temporary and copied once is written where it goes;
 * aliasing the compiler must respect gives the same results as clang. */
int printf(const char *, ...);
typedef struct { int a, b, c, d, e, f; } six;
typedef struct { int found; six s; const six *from; } wrapped;
#define N __attribute__((noinline)) static
static six g;
static six build(const six *p)
{
    six t = { p->b + 1, p->c * 2, p->d - 3, p->e, p->f, p->a };
    return t;
}
static six swapped(const six *p)
{
    six t;
    t.a = p->f;
    t.b = p->e;
    t.c = t.a + p->d; /* (reads its own field) */
    t.d = p->c;
    t.e = t.c * 3;
    t.f = p->a;
    return t;
}
N six from_global(void) { return build(&g); }
N wrapped wrap(const six *p)
{
    wrapped w = { 1, build(p), p };
    return w;
}
N six twice(const six *p, int k)
{
    if (k & 1)
        return swapped(p);
    six t = build(p);
    return swapped(&t);
}
/* the destination is read while the temporary is filled */
N void into_self(six *p) { *p = build(p); }
N void through(six *p, six *q)
{
    six t = swapped(p);
    q->a += 100; /* q may be p, the destination of the copy */
    t.b += q->a;
    *p = t;
}
N int local_chain(const six *p)
{
    six x = build(p), y;
    y = x;
    x.a = 1000;
    six z = y;
    return z.a + y.b * 3 + x.a * 7 + z.f * 11;
}
static unsigned h;
N void mix(const six *s)
{
    const int *w = &s->a;
    for (int i = 0; i < 6; i++)
        h = h * 31 + (unsigned)w[i];
}
int main(void)
{
    for (int i = 0; i < 20; i++) {
        six v = { i, i * 7, i * 13, i * 19, i * 23, i * 29 }, r;
        g = v;
        r = from_global();
        mix(&r);
        g = from_global(); /* the result over what it was built from */
        mix(&g);
        g = twice(&g, i);
        mix(&g);
        v = wrap(&v).s;
        mix(&v);
        v = twice(&v, i + 1);
        mix(&v);
        into_self(&v);
        mix(&v);
        through(&v, &v);
        mix(&v);
        through(&v, &g);
        mix(&v);
        v = swapped(&v);
        mix(&v);
        h = h * 31 + (unsigned)local_chain(&v);
    }
    printf("%08x\n", h);
    return 0;
}
