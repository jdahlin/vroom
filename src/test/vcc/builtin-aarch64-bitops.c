#if defined(__aarch64__)
static volatile unsigned long long input = 0x1200000000000040ull;

static unsigned long long reverse(unsigned long long value, unsigned width)
{
    unsigned long long result = 0;
    for (unsigned i = 0; i < width; ++i) {
        result = (result << 1) | (value & 1);
        value >>= 1;
    }
    return result;
}

int main(void)
{
    unsigned long long value = input;
    if (__builtin_arm_rbit((unsigned)value) != reverse(value, 32))
        return 1;
    if (__builtin_arm_rbit64(value) != reverse(value, 64))
        return 2;
    if (__builtin_arm_clz((unsigned)value) != 25)
        return 3;
    if (__builtin_arm_clz64(value) != 3)
        return 4;
    return 0;
}
#else
int main(void) { return 0; }
#endif
