/* __builtin_prefetch: its address is evaluated once, with its side effects, a
 * bad address does not fault, and the loads around it are unchanged. */
int printf(const char *, ...);
static int calls;
__attribute__((noinline)) static const int *next(const int *p)
{
    calls++;
    return p + 1;
}
__attribute__((noinline)) static int walk(const int *p, int n)
{
    int sum = 0, i = 0;

    while (i < n) {
        __builtin_prefetch(&p[i + 16]);
        __builtin_prefetch(p + i, 1);
        __builtin_prefetch(p + i, 0, 0);
        __builtin_prefetch(p + i++, 1, 1);
        sum += p[i - 1];
        __builtin_prefetch(next(p), 0, 2);
        sum += p[i - 1] * 3;
    }
    return sum;
}
int main(void)
{
    static int a[64];

    for (int i = 0; i < 64; i++)
        a[i] = i * 7 + 1;
    __builtin_prefetch((void *)0);
    __builtin_prefetch((void *)-1, 1, 3);
    printf("%d %d\n", walk(a, 64), calls);
    return 0;
}
