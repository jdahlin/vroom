/* Small records passed by value: each of 1 to 8 bytes, and scalars next to
 * them that must still arrive extended, and returned by value; results the
 * same as clang's. */
int printf(const char *, ...);
typedef struct { signed char a; } r1;
typedef struct { unsigned short a; } r2;
typedef struct { signed char a, b, c; } r3;
typedef struct { short a, b; } r4;
typedef struct { short a, b, c; } r6;
typedef struct { r2 m; signed char t; } r4b;
typedef struct { int x; float y; } r8;
#define N __attribute__((noinline)) static
N long f1(r1 v, signed char s, short h) { return v.a * 7 + s * 3 + h; }
N long f2(r2 v, r2 w) { return v.a * 100000L + w.a; }
N long f3(r3 v, r4 w) { return v.a + v.b * 5 + v.c * 11 + w.a * 13 + w.b; }
N long f6(r6 v, r4b w, r8 z) { return v.a + v.b * 3 + v.c * 9 + w.m.a + w.t * 27 + z.x + (long)z.y; }
N long pass(const r1 *a, const r2 *b, const r3 *c, const r4 *d, const r6 *e, const r4b *g, int i)
{
    r1 x = a[i];
    r2 y = b[i];
    r8 z = { i, 2.5f * (float)i };

    return f1(x, (signed char)(i * 37), (short)(i * 1021)) + f2(y, b[i + 1]) +
           f3(c[i], d[i]) + f6(e[i], g[i], z) + f2(y, y) + f1(x, x.a, y.a);
}
N r1 g1(const r1 *p, int i) { return p[i]; }
N r2 g2(const r2 *p, int i)
{
    r2 none = { 0 };

    if (i & 1)
        return none;
    return p[i];
}
N r3 g3(const r3 *p, int i) { return p[i]; }
N r4 g4(short x) { r4 v = { x, (short)-x }; return v; }
N r6 g6(const r6 *p, int i) { return p[i]; }
N r4b g4b(const r4b *p, int i) { return p[i]; }
int main(void)
{
    r1 a[9]; r2 b[9]; r3 c[9]; r4 d[9]; r6 e[9]; r4b g[9];
    long h = 0;

    for (int i = 0; i < 9; i++) {
        a[i].a = (signed char)(i * 61 - 100);
        b[i].a = (unsigned short)(i * 9001 + 5);
        c[i] = (r3){ (signed char)-i, (signed char)(i * 50), 7 };
        d[i] = (r4){ (short)(-i * 999), (short)(i * 3) };
        e[i] = (r6){ 1, (short)-i, (short)(i * 77) };
        g[i].m.a = (unsigned short)(i + 40000);
        g[i].t = (signed char)-i;
    }
    for (int i = 0; i < 8; i++)
        h = h * 31 + pass(a, b, c, d, e, g, i);
    for (int i = 0; i < 8; i++) {
        r4 q = g4((short)(i * 7001));
        r6 w = g6(e, i);
        r4b z = g4b(g, i);

        h = h * 31 + g1(a, i).a + g2(b, i).a * 3 + g3(c, i).b * 5 + q.a +
            q.b * 7 + w.c + z.m.a + z.t;
    }
    printf("%ld\n", h);
    return 0;
}
