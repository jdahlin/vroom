/* vcc-flags: -mavx512f */
/* abi-targets: linux; AVX-512 is x86-64's */
#include <stdio.h>
#include "avx512.h"

static void show(const char *what, v16si v)
{
    printf("%s", what);
    for (int k = 0; k < 16; k++)
        printf(" %d", v[k]);
    printf("\n");
}

v16si times(v16si x, int by)
{
    return x * by;
}

__attribute__((target("avx512bw"))) static void show_bytes(v64qi a)
{
    v64qi v = bytes(a, a);
    printf("bytes %d %d\n", v[1], v[63]);
}

int main(void)
{
    v16si a, b;
    v16sf f;
    v8df d;
    v64qi c;
    v8si half;
    v32si w;
    v64si x4;
    for (int k = 0; k < 16; k++) {
        a[k] = k + 1;
        b[k] = 10 * k;
        f[k] = (float)k;
    }
    for (int k = 0; k < 8; k++) {
        d[k] = k + 0.5;
        half[k] = 7 * k;
    }
    for (int k = 0; k < 64; k++) {
        c[k] = (char)k;
        x4[k] = 3 * k;
    }
    for (int k = 0; k < 32; k++)
        w[k] = 2 * k;
    show("add", add(a, b));
    v16sf m = mix(1, f, 2.0, d, 3, c);
    printf("mix");
    for (int k = 0; k < 16; k++)
        printf(" %g", (double)m[k]);
    printf("\n");
    v2di wide = {100, 200};
    show("many", many(a, b, a, b, a, b, a, b, 7, a, wide, half, b));
    struct box x = {a};
    show("boxed", boxed(x, 4).v);
    struct arrayed ar = {{b}};
    show("arrayed", arrayed(ar).v[0]);
    struct halves h = {half, half * 2};
    h = halves(h);
    printf("halves %d %d\n", h.a[7], h.b[7]);
    union overlaid u;
    u.v = a;
    show("overlaid", overlaid(u).v);
    w = twice(w);
    printf("twice %d %d\n", w[1], w[31]);
    x4 = thrice(x4);
    printf("thrice %d %d\n", x4[1], x4[63]);
    printf("variadic %d\n", variadic(3, a, b, a));
    show("named", named_variadic(a, 2, 100, 1000));
    show("callback", callback(times, a));
    show_bytes(c);
    return 0;
}
