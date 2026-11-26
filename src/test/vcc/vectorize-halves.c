/* The vectorizer's 16-bit lanes (-O3): every trip count around the lane count, wraparound,
   invariants, and arrays that overlap. */
#include <stdio.h>

typedef short i16;
typedef unsigned short u16;

__attribute__((noinline)) static void add(i16 *a, const i16 *w, int n)
{
    for (int i = 0; i < n; i++)
        a[i] += w[i];
}
__attribute__((noinline)) static void update(i16 *o, const i16 *in,
                                             const i16 *r0, const i16 *a0,
                                             const i16 *r1, int n)
{
    for (int i = 0; i < n; i++)
        o[i] = (in[i] + a0[i]) - (r0[i] + r1[i]);
}
__attribute__((noinline)) static void bits(u16 *a, const u16 *b, u16 k,
                                           long long wide, int n)
{
    for (int i = 0; i < n; i++)
        a[i] = ((a[i] - k) ^ 0x1234) | (b[i] & wide);
}

static unsigned hash(const void *p, int bytes)
{
    const unsigned char *c = p;
    unsigned h = 17;
    for (int i = 0; i < bytes; i++)
        h = h * 31 + c[i];
    return h;
}

static i16 a[128], b[128], c[128], d[128], o[128];

static void fill(void)
{
    for (int i = 0; i < 128; i++) {
        a[i] = (i16)(i * 12345 + 30000);
        b[i] = (i16)(i * 777 - 32000);
        c[i] = (i16)(i * 3);
        d[i] = (i16)(32767 - i);
        o[i] = -1;
    }
}

int main(void)
{
    for (int n = 0; n <= 40; n++) {
        fill();
        add(a, b, n);
        update(o, a, b, c, d, n);
        bits((u16 *)c, (const u16 *)d, 40000, 0x700ff00ffLL, n);
        printf("%d %u %u %u\n", n, hash(a, sizeof a), hash(o, sizeof o),
               hash(c, sizeof c));
    }
    /* overlaps: forwards, backwards and onto itself */
    for (int shift = -9; shift <= 9; shift++) {
        fill();
        add(a + 20 + shift, a + 20, 64);
        update(b + 20, b + 20 + shift, c, d, b + 20, 64);
        printf("%d %u %u\n", shift, hash(a, sizeof a), hash(b, sizeof b));
    }
    return 0;
}
