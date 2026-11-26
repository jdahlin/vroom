/* p[off + i] with 32-bit unsigned off (-O3), vectorized behind a test that off + i does not
   wrap in the loop.  (A wrapping case needs 4 Gi elements of address space, which this
   suite's runtime cannot map.) */
#include <stdio.h>

__attribute__((noinline)) static void sub16(short *a, const short *w,
                                            unsigned off, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        a[i] -= w[off + i];
}
__attribute__((noinline)) static void add32(int *a, const int *w, unsigned off,
                                            unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        a[i] += w[off + i];
}
__attribute__((noinline)) static void both(int *a, const int *w, unsigned x,
                                           unsigned y, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        a[x + i] = w[y + i] + 1;
}

static unsigned hash(const void *p, int bytes)
{
    const unsigned char *c = p;
    unsigned h = 17;
    for (int i = 0; i < bytes; i++)
        h = h * 31 + c[i];
    return h;
}

static short ws[256], as[64];
static int wi[256], ai[64];

int main(void)
{
    for (int i = 0; i < 256; i++) {
        ws[i] = (short)(i * 999);
        wi[i] = i * 100003;
    }
    for (unsigned n = 0; n <= 40; n++) {
        sub16(as, ws, n * 3, n);
        add32(ai, wi, n * 5, n);
        both(ai, wi, n % 7, n % 11, n);
        printf("%u %u %u\n", n, hash(as, sizeof as), hash(ai, sizeof ai));
    }
    return 0;
}
