static int effects;
static __attribute__((noinline)) int guarded(int x)
{
#if defined(__x86_64__)
    if (__builtin_constant_p(x) && x != 0)
        __asm__ volatile ("# %0" : : "i" (x));
    else
#endif
        ++effects;
    if ((++effects, 0)) ++effects;
    else effects += 2;
    if (1) ++effects;
    else effects += 100;
    return effects;
}
static int label_entry(void)
{
    goto enter;
    if (0) {
enter:
        return 7;
    }
    return 0;
}
static int case_entry(int x)
{
    switch (x) {
        if (0) { case 3: return 9; }
        default: return 0;
    }
}
int main(void)
{
    volatile int input = 3;
    if (guarded(input) != 5) return 1;
    if (label_entry() != 7) return 2;
    if (case_entry(3) != 9 || case_entry(2) != 0) return 4;
    return 0;
}
