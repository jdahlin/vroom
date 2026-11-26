/* Loads through a choice of locals' addresses (inlined min, max and clamp by reference):
   the values are chosen instead, unless something writes in between. */
#include <stdio.h>

static inline const int *mn(const int *a, const int *b) { return *b < *a ? b : a; }
static inline const int *cl(const int *v, const int *l, const int *h)
{
    return *v < *l ? l : *h < *v ? h : v;
}
static inline const short *cls(const short *v, const short *l, const short *h)
{
    return *v < *l ? l : *h < *v ? h : v;
}

__attribute__((noinline)) static int clamp(int x)
{
    int v = x, lo = 0, hi = 254;
    return *cl(&v, &lo, &hi);
}
__attribute__((noinline)) static int clamp_short(short x)
{
    short v = x, lo = -3, hi = 254;
    return *cls(&v, &lo, &hi);
}
__attribute__((noinline)) static int nested(int a, int b, int c)
{
    return *mn(mn(&a, &b), &c);
}
/* the chosen slot changes between the choice and the load */
__attribute__((noinline)) static int written(int a, int b)
{
    const int *p = mn(&a, &b);
    a += 100;
    b += 200;
    return *p;
}
__attribute__((noinline)) static int written_in_branch(int a, int b, int k)
{
    const int *p;
    if (k)
        p = &a;
    else
        p = &b;
    if (k > 1) {
        a = 7;
        b = 8;
    }
    return *p;
}
__attribute__((noinline)) static int looped(int n)
{
    int a = 1, b = 2;
    int *p = &a;
    int s = 0;
    for (int i = 0; i < n; i++) {
        s = s * 10 + *p;
        a += 2;
        if (p == &a)
            p = &b;
        else
            p = &a;
    }
    return s;
}

int main(void)
{
    printf("%d %d %d\n", clamp(-5), clamp(100), clamp(999));
    printf("%d %d %d\n", clamp_short(-5), clamp_short(100), clamp_short(999));
    printf("%d %d %d\n", nested(3, 2, 1), nested(1, 2, 3), nested(2, 1, 3));
    printf("%d %d\n", written(1, 2), written(5, 4));
    printf("%d %d %d\n", written_in_branch(1, 2, 0), written_in_branch(1, 2, 1),
           written_in_branch(1, 2, 2));
    printf("%d\n", looped(5));
    return 0;
}
