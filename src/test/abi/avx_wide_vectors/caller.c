/* vcc-flags: -mavx2 */
/* abi-targets: linux; AVX is x86-64's */
#include <stdio.h>
#include "wide.h"

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

int main(void)
{
    v16si a, b;
    v16df d;
    v32di l;
    for (int k = 0; k < 16; k++) {
        a[k] = k + 1;
        b[k] = 100 * k;
        d[k] = k + 0.25;
    }
    for (int k = 0; k < 32; k++)
        l[k] = 1000 + k;
    show("add", add(a, 3, b));
    v16df e = twice(d);
    printf("twice");
    for (int k = 0; k < 16; k++)
        printf(" %g", e[k]);
    printf("\n");
    v32di m = thrice(l);
    printf("thrice");
    for (int k = 0; k < 32; k++)
        printf(" %lld", m[k]);
    printf("\n");
    v2ti t = {11, 22};
    t = swapped(t);
    printf("swapped %d %d\n", (int)t[0], (int)t[1]);
    struct box x = {a};
    show("boxed", boxed(x).v);
    printf("variadic %d\n", variadic(3, a, b, a));
    show("named", named_variadic(a, 2, 100, 1000));
    show("callback", callback(times, a));
    return 0;
}
