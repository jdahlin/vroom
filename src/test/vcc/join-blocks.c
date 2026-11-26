/* Inlined comparisons leave chains of blocks; joined, the compare fuses with its branch. */
#include <stdio.h>

static inline _Bool lt(short a, short b) { return a < b; }
static inline short cl(short v) { return lt(v, 0) ? 0 : lt(254, v) ? 254 : v; }

__attribute__((noinline)) static int sum(const short *p, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++)
        s = s * 3 + cl(p[i]);
    return s;
}
__attribute__((noinline)) static int computed(int k)
{
    static void *labels[] = {&&one, &&two};
    int r = 0;
    goto *labels[k];
one:
    r += cl(-4);
two:
    r += cl(1000);
    return r;
}

int main(void)
{
    short v[] = {-5, 0, 7, 254, 300, 12};
    printf("%d %d %d\n", sum(v, 6), computed(0), computed(1));
    return 0;
}
