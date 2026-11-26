/* A vector load or store touches all 16 bytes: scalar reads of the same
   memory around vector operations, in a block, across blocks and in a loop,
   see what was written; unaligned and aliasing operands. */
#include <stdio.h>
#include <string.h>

typedef int v4 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));
typedef double d2 __attribute__((vector_size(16)));
typedef long long l2 __attribute__((vector_size(16)));
typedef v4 uv4 __attribute__((aligned(1)));

__attribute__((noinline)) static int across(v4 *p, const v4 *a)
{
    int *q = (int *)p;
    int before = q[3];
    *p = *a + *a;
    return before * 1000 + q[3];
}

__attribute__((noinline)) static int looped(v4 *p, const v4 *a, int n)
{
    int *q = (int *)p;
    int sum = 0;
    q[3] = 5;
    for (int i = 0; i < n; i++) {
        sum = sum * 3 + q[3];
        *p = *p + *a;
        if (sum & 1)
            sum += q[2];
    }
    return sum + q[3] + q[1];
}

__attribute__((noinline)) static int slots(int k)
{
    union {
        v4 v;
        int i[4];
    } u, w;
    for (int i = 0; i < 4; i++)
        u.i[i] = k + i, w.i[i] = 1;
    int old = u.i[3];
    u.v = u.v + w.v;
    int mid = u.i[3];
    u.v = u.v * u.v - w.v;
    return old * 10000 + mid * 100 + u.i[3];
}

__attribute__((noinline)) static void aliased(v4 *p)
{
    *p = *p + *p;
    *p = (*p ^ *p) | (*p & *p);
}

__attribute__((noinline)) static void unaligned(unsigned char *out,
                                                const unsigned char *in)
{
    *(uv4 *)out = *(const uv4 *)in + *(const uv4 *)(in + 4);
}

__attribute__((noinline)) static void reals(f4 *f, d2 *d, l2 *l)
{
    *f = (*f + *f) * *f / (*f - (*f + *f));
    *d = *d * *d + *d;
    *l = (*l + *l) - (*l ^ *l);
}

int main(void)
{
    v4 p = {1, 2, 3, 4}, a = {10, 20, 30, 40};
    printf("%d\n", across(&p, &a));
    printf("%d\n", looped(&p, &a, 6));
    printf("%d %d\n", slots(3), slots(-9));
    aliased(&p);
    printf("%d %d %d %d\n", p[0], p[1], p[2], p[3]);
    unsigned char in[24], out[20];
    for (int i = 0; i < 24; i++)
        in[i] = (unsigned char)(i * 7);
    unaligned(out + 1, in + 1);
    unsigned h = 0;
    for (int i = 1; i < 17; i++)
        h = h * 31 + out[i];
    printf("%u\n", h);
    f4 f = {1, 2, 3, 4};
    d2 d = {1.5, -2};
    l2 l = {5000000000LL, -7};
    reals(&f, &d, &l);
    printf("%g %g %g %g %lld %lld\n", (double)f[0], (double)f[3], d[0], d[1],
           l[0], l[1]);
    return 0;
}
