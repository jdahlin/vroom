/* A 128-bit vector held in a local and used as several lane types (__m128i passed through
   intrinsics): the local becomes a value, bitcast between the types. */
int printf(const char *, ...);

typedef long long v2 __attribute__((vector_size(16)));
typedef int v4 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));

static inline v2 add32(v2 a, v2 b) { return (v2)((v4)a + (v4)b); }
static inline v2 sub64(v2 a, v2 b) { return a - b; }

__attribute__((noinline)) static v2 sum(const v2 *p, int n)
{
    v2 x = {0, 0};
    for (int i = 0; i < n; i++)
        x = add32(x, p[i]);
    return x;
}
__attribute__((noinline)) static v2 mixed(v2 a, v2 b, int k)
{
    v2 x = a;
    if (k & 1)
        x = (v2)((v4)x ^ (v4)b);
    else
        x = sub64(x, b);
    f4 f = (f4)x;
    if (k & 2)
        x = (v2)f;
    return add32(x, a);
}
__attribute__((noinline)) static v2 swap(v2 a, v2 b, int n)
{
    v2 x = a;
    v4 y = (v4)b;
    for (int i = 0; i < n; i++) {
        v2 t = x;
        x = (v2)y;
        y = (v4)t + (v4)x;
    }
    return add32(x, (v2)y);
}

static void show(v2 v)
{
    printf("%llx %llx\n", (unsigned long long)v[0], (unsigned long long)v[1]);
}

int main(void)
{
    v2 p[5] = {{0x1ffffffffLL, 2},
               {-1, 0x80000000LL},
               {3, 4},
               {0x7fffffff7fffffffLL, -5},
               {6, 7}};
    for (int n = 0; n <= 5; n++)
        show(sum(p, n));
    for (int k = 0; k < 4; k++)
        show(mixed(p[0], p[3], k));
    for (int n = 0; n < 6; n++)
        show(swap(p[1], p[2], n));
    return 0;
}
