#if defined(__x86_64__)
static volatile unsigned long long input;
static unsigned long long sum;
static unsigned evaluations;

__attribute__((noinline)) static unsigned long long arg(unsigned long long value)
{
    ++evaluations;
    return value;
}

__attribute__((noinline)) static unsigned long long *out(void)
{
    ++evaluations;
    return &sum;
}

int main(void)
{
    input = ~0ULL;
    if (__builtin_ia32_addcarryx_u64(arg(0), arg(input), arg(1), out()) != 1 ||
        sum != 0 || evaluations != 4)
        return 1;
    if (__builtin_ia32_addcarryx_u64(arg(255), arg(5), arg(7), out()) != 0 ||
        sum != 13 || evaluations != 8)
        return 2;
    if (__builtin_ia32_addcarryx_u64(arg(1), arg(input), arg(input), out()) != 1 ||
        sum != ~0ULL || evaluations != 12)
        return 3;
    if (__builtin_ia32_addcarryx_u64(0, sum, 1, &sum) != 1 || sum != 0)
        return 4;
    if (__builtin_ia32_addcarryx_u64(arg(256), arg(5), arg(7), out()) != 0 ||
        sum != 12 || evaluations != 16)
        return 5;
    if (__builtin_ia32_addcarryx_u64(1, input, 0, &sum) != 1 || sum != 0)
        return 6;
    return 0;
}
#else
int main(void) { return 0; }
#endif
