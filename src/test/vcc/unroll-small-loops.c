/* Loops with a small constant trip count (fully unrolled at -O2): zero and one trip, counting
   down and by strides, unsigned and 64-bit counters, the counter and carried values after the
   loop, nesting, and arrays of scalars and vectors. */
int printf(const char *, ...);

typedef int v4 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));

__attribute__((noinline)) static int counts(int x)
{
    int s = 0, i;
    for (i = 0; i < 0; i++)
        s += 100;
    for (i = 3; i < 4; i++)
        s = s * 7 + i + x;
    int after = i;
    for (i = 7; i > 0; i -= 2)
        s = s * 7 + i;
    after = after * 10 + i;
    for (unsigned u = 5; u-- != 0;)
        s = s * 3 + (int)u;
    unsigned u;
    for (u = 0xfffffffeu; u != 2; u++)
        s = s * 5 + (int)u;
    for (long long l = -6; l <= 6; l += 4)
        s ^= (int)(l * x);
    for (i = 0; i < 8; i++)
        s += i & x;
    for (i = 0; i < 9; i++)
        s += i;
    return s * 31 + after * 7 + (int)u;
}

__attribute__((noinline)) static int carried(int a, int b, int c)
{
    for (int i = 0; i < 5; i++) {
        int t = a;
        a = b;
        b = c;
        c = t + i;
    }
    return a * 10000 + b * 100 + c;
}

__attribute__((noinline)) static int arrays(const int *p, int n)
{
    int a[8], total = 0;
    for (int k = 0; k < 8; k++)
        a[k] = p[k];
    for (int j = 0; j < n; j++)
        for (int k = 0; k < 8; k++)
            a[k] += p[k] * j;
    for (int k = 7; k >= 0; k--)
        total = total * 3 + a[k];
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 4; k++)
            total += a[k + i] * (i + 1);
    return total;
}

__attribute__((noinline)) static void vectors(v4 *out, const v4 *in, int n)
{
    v4 acc[4] = {0};
    f4 f[2] = {0};
    for (int j = 0; j < n; ++j)
        for (int k = 0; k < 4; ++k)
            acc[k] += in[(j + k) % 4];
    for (int k = 0; k < 2; ++k)
        f[k] += (f4)acc[k] * 0.0f;
    for (int k = 0; k < 4; ++k)
        out[k] = acc[k] + (v4)f[k % 2];
}

static void show(const v4 *v)
{
    for (int k = 0; k < 4; k++)
        printf("%d %d %d %d\n", v[k][0], v[k][1], v[k][2], v[k][3]);
}

int main(void)
{
    int p[16] = {3, -1, 4, 1, -5, 9, 2, -6, 5, 3, 5};
    v4 in[4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {-1, -2, -3, -4}, {100, 0, 0, 7}};
    v4 out[4];
    for (int x = -2; x < 3; x++)
        printf("%d %d %d\n", counts(x), carried(x, 2, 3),
               arrays(p, x + 2));
    for (int n = 0; n < 4; n++) {
        vectors(out, in, n);
        show(out);
    }
    return 0;
}
