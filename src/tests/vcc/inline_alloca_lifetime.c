/* Inlined function-lifetime alloca must not accumulate across calls.
   Early returns, nested inline calls and caller VLA saves share that rule. */
extern void abort(void);
__attribute__((noinline)) static int use(char *p, int n)
{
    p[0] = 17;
    p[n - 1] = 29;
    __asm__ volatile ("" : : "r"(p) : "memory");
    return p[0] + p[n - 1];
}
__attribute__((always_inline)) static inline int inner(int n)
{
    char *p = __builtin_alloca(n);
    int r = use(p, n);
    if (n & 1) return r;
    return r + 1;
}
__attribute__((always_inline)) static inline int outer(int n)
{
    char *p = __builtin_alloca(64);
    p[0] = 3;
    int r = inner(n);
    return r + p[0];
}
__attribute__((noinline)) static void run(int n)
{
    char *persistent = __builtin_alloca(64);
    persistent[0] = 9;
    {
        volatile char caller_vla[n];
        caller_vla[0] = 5;
        for (int i = 2; i < 8192; ++i)
            if (outer(i) != 49 + !(i & 1)) abort();
        if (caller_vla[0] != 5) abort();
    }
    if (persistent[0] != 9) abort();
}
int main(void) { run(37); return 0; }
