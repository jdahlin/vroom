/* vcc-flags: -mavx2 */
/* abi-targets: linux; AVX is x86-64's */
#include <stdio.h>
#include "avx.h"

static void show(const char *what, v8si v)
{
    printf("%s", what);
    for (int k = 0; k < 8; k++)
        printf(" %d", v[k]);
    printf("\n");
}

v8si times(v8si x, int by)
{
    return x * by;
}

int main(void)
{
    v8si a = {1, 2, 3, 4, 5, 6, 7, 8}, b = {10, 20, 30, 40, 50, 60, 70, 80};
    v8sf f = {1, 2, 3, 4, 5, 6, 7, 8};
    v4df d = {0.5, 1.5, 2.5, 3.5};
    v32qi c;
    for (int k = 0; k < 32; k++)
        c[k] = (char)k;
    show("add", add(a, b));
    v8sf m = mix(1, f, 2.0, d, 3, c);
    printf("mix");
    for (int k = 0; k < 8; k++)
        printf(" %g", (double)m[k]);
    printf("\n");
    v2di wide = {100, 200};
    show("many", many(a, b, a, b, a, b, a, b, 7, a, wide, b));
    struct box x = {a};
    show("boxed", boxed(x, 4).v);
    struct pair p = {a, b};
    struct pair q = paired(p);
    show("paired", q.a);
    show("paired", q.b);
    printf("variadic %d\n", variadic(3, a, b, a));
    show("named", named_variadic(a, 2, 100, 1000));
    show("callback", callback(times, a));
    return 0;
}
