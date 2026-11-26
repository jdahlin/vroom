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

__attribute__((noinline)) static void opaque(int *p)
{
    *p += 1;
}

/* vectors as loop values, live across a call, chosen by a branch */
__attribute__((noinline)) static int carried(const v4 *a, int n)
{
    v4 sum = {0, 0, 0, 0}, other = {1, 1, 1, 1};
    int calls = 0;
    for (int i = 0; i < n; i++) {
        v4 x = a[i % 3];
        if (i & 1)
            sum = sum + x;
        else
            sum = sum ^ x;
        opaque(&calls);
        v4 swapped = other;
        other = sum;
        sum = sum + swapped;
    }
    union {
        v4 v;
        int i[4];
    } u;
    u.v = sum + other;
    return u.i[0] + u.i[1] * 3 + u.i[2] * 5 + u.i[3] * 7 + calls;
}

/* more vectors live at once than there are registers */
__attribute__((noinline)) static void pressure(v4 *out, const v4 *in)
{
    v4 a = in[0] + in[1], b = in[1] + in[2], c = in[2] + in[3];
    v4 d = in[3] + in[4], e = in[4] + in[5], f = in[5] + in[6];
    v4 g = in[6] + in[7], h = in[7] + in[8], i = in[8] + in[9];
    v4 j = in[9] + in[10], k = in[10] + in[11], l = in[11] + in[12];
    v4 m = in[12] + in[13], n = in[13] + in[14], o = in[14] + in[15];
    v4 p = in[15] + in[0], q = in[0] * in[3], r = in[1] * in[5];
    int dummy = 0;
    opaque(&dummy);
    *out = (a - b) ^ (c - d) ^ (e - f) ^ (g - h) ^ (i - j) ^ (k - l) ^
           (m - n) ^ (o - p) ^ (q + r) ^ (a + c + e + g + i + k + m + o) ^
           (b + d + f + h + j + l + n + p);
}

int main(void)
{
    v4 many[16], mixed;
    for (int i = 0; i < 16; i++)
        many[i] = (v4){i * 3, i * 5 + 1, 100 - i, i * i};
    printf("%d %d\n", carried(many, 7), carried(many + 5, 12));
    pressure(&mixed, many);
    printf("%d %d %d %d\n", mixed[0], mixed[1], mixed[2], mixed[3]);
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
